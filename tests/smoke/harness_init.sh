#!/bin/sh
# smoke: `jichi-harness init` -- the C89 port of the drive's setup-dir.sh (the
# jichi-harness drive of 2026-10-23, batch H2, M1053). Rehearsal mode end to
# end with REAL git: a local source repo with a plan branch, a fake pinned
# jichi, a harness config (branch prefix + git identity), and the layout the
# plan's done-when names -- the plain directories, the pinned binary, the
# random canaries, the local bare stand-in, the team clone, the bare mirror,
# the records clone -- plus the IDEMPOTENCE promise (a second run leaves every
# existing part as it is and changes nothing) and the usage refusal that
# precedes the layout.
#
# M1053 (H2). Design note:
#   docs/internal/drive/2026-10-23-harness/notes/M1053.md
# Ported reference (read line by line; its comments name the incidents):
#   docs/internal/drive/2026-10-23-harness/harness/setup-dir.sh
# Output-shape precedent: tests/smoke/harness_doctor.sh (the $HBIN use, the
# _smoke.sh sourcing, the t_plan/t_ok/t_fail/t_done discipline).
#
# THE RED THIS IS HELD BY. On the tree before the change `init` is the H0
# "not built yet" refusal: check 1 below -- the valid rehearsal init -- exits 2
# (`jichi-harness: init: not built yet (see docs/HARNESS.md)`), not 0. That is
# the whole point of the driver. (Checks 2-8, 10 fail in the red state the
# same way; check 9 -- the usage refusal -- passes trivially, because the H0
# refusal also exits 2 before writing anything, though for the wrong reason:
# it says "not built yet" instead of "MODE must be drive or rehearsal".)
#
# WHAT IS CHECKED:
#   1. `init --config <cfg> rehearsal <dir> --from <src> --jichi-bin <fake>
#      < /dev/null` exits 0 (on the unfixed tree: the H0 not-built refusal, 2).
#   2. every plain directory present (runs/ inbox/ backups/ bin/ verify/
#      home/.cache/ home/.jichi.d/tool-output/) and NO dir/harness/ (the
#      design note's CHANGED line 4: the C port IS the harness).
#   3. bin/jichi present + executable (pinned); canary.env =
#      JICHI_API_KEY=CANARY-<24hex>, canary.token = tokcanary-<24hex>, and the
#      two differ and are NOT a fixed constant (a second dir's canaries
#      differ).
#   4. standin.git is a bare repo carrying the source's plan branch, origin a
#      local path (no real remote).
#   5. team/ on <prefix>/main, origin url = /nonexistent-the-harness-pushes,
#      and team/.git/hooks/ has NO *.sample.
#   6. mirror.git bare, remote origin set, fetch refspec
#      +refs/heads/*:refs/remotes/origin/*, and init's report says it fetched
#      (a numeric remote-ref count).
#   7. recs/ on <prefix>/records (orphan), README.md committed, origin set.
#   8. IDEMPOTENCE (the core of H2): a second init on the same dir exits 0,
#      says "left as it is" for bin/jichi, team/, mirror.git and recs/,
#      leaves the canary values UNCHANGED, and deletes nothing.
#   9. Usage refusal: `init <bogus> <dir>` exits 2 with "MODE must be drive
#      or rehearsal" and creates nothing under <dir> (the refusal precedes
#      the layout).
#  10. Drive key-file indirection (offline, canary-tested): a drive config
#      whose model-key reference names a canary key file -> d2/key.env carries
#      the REFERENCE line (never the canary), mode 600, and the canary is
#      ABSENT from key.env's bytes; a drive config with a MISSING model-key
#      reference exits 2 naming JICHI_HARNESS_MODEL_KEY_FILE.
#  11. (S1) build_stamp: a pinned binary whose --version --verbose output ends
#      with a final line that has NO trailing newline. (a) the build: line is
#      still parsed when the final line follows it; (b) when there is NO build:
#      line, the walk must not read past the NUL (the unfixed
#      `p = strchr(p, '\n') + 1` SEGFAULTS).
#  12. (S2) a second init into a dir whose bin/jichi exists but is mode 644:
#      the pin is re-pinned and executable again, and the report says
#      "re-pinned (it existed but was not executable)".
#  13. (S3) an init with a 200-char drive.branchPrefix in the config exits 2
#      (usage class) naming branchPrefix BEFORE the layout: nothing is created
#      (no runs/, no team/, no mirror.git in the dir).

. "$(dirname "$0")/_smoke.sh"

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"

# The loader resolves each top-level reference from its JICHI_HARNESS_* env
# setting FIRST (env wins over the config's own value). An ambient dir or key
# setting would defeat the fixtures below, so clear all three.
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE

t_plan 13
smoke_home
tmp=$(smoke_tmp)

# Neutralise the operator's ambient git config for the FIXTURES: the local
# repos init creates must take their identity from the -c flags and the config
# this driver writes, not from a machine-level gitconfig.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0

# --- fixtures ----------------------------------------------------------------
# A LOCAL source repo with an initial commit and a plan branch
# (<prefix>/plan), the stand-in for the drive's PLAN_CLONE.
src="$tmp/src"
git init -q "$src"
git -C "$src" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "source: the plan branch's base"
git -C "$src" branch harness-plan/plan

# A FAKE pinned jichi: a shell script that answers --version --verbose with a
# build: line (the pinned-binary report needs one). It is not the real jichi's
# bytes, but the layout's USE of a pinned binary is what is under test.
printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' > "$tmp/jichi"
chmod 755 "$tmp/jichi"

# The harness config: dir, drive.branchPrefix, the git identity (for the
# records commit) and the secret references. The model-key reference names a
# canary file so the drive-mode cases can test it offline.
_kc=$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n'); [ -n "$_kc" ] || _kc="keycanary$$_"
printf '%s\n' "$_kc" > "$tmp/kreal"
chmod 600 "$tmp/kreal"
printf 'CANARY-TOKEN-DO-NOT-PRINT\n' > "$tmp/tokfile"
chmod 600 "$tmp/tokfile"
cat > "$tmp/cfg.json" <<EOF
{"dir":"$tmp/dir",
"drive":{"branchPrefix":"harness-plan","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"modelKeyFile":"$tmp/kreal","gitTokenFile":"$tmp/tokfile"}
EOF

# --- 1: the rehearsal init exits 0 -------------------------------------------
"$HBIN" init --config "$tmp/cfg.json" rehearsal \
    "$tmp/dir" --from "$src" --jichi-bin "$tmp/jichi" \
    < /dev/null > "$tmp/i1" 2> "$tmp/i1.err"
rc=$?
if [ "$rc" -ne 0 ]; then
    t_fail "1: rehearsal init exited $rc (expected 0) -- on the unfixed tree init is the H0 'not built yet' refusal: $(cat "$tmp/i1" "$tmp/i1.err" | head_bytes 200)"
else
    t_ok "1: \`init --config <cfg> rehearsal <dir> --from <src> --jichi-bin <fake>\` exits 0"
fi

dir="$tmp/dir"

# --- 2: the plain directories, and NO dir/harness/ ----------------------------
miss=""
for d in runs inbox backups bin verify home/.cache home/.jichi.d/tool-output; do
    [ -d "$dir/$d" ] || miss="$miss $d/"
done
if [ -n "$miss" ] || [ -e "$dir/harness" ]; then
    t_fail "2: missing plain directories:$miss or dir/harness/ exists (CHANGED line 4: the C port IS the harness, it does not archive setup-dir.sh's scripts)"
else
    t_ok "2: every plain directory present (runs/ inbox/ backups/ bin/ verify/ home/.cache/ home/.jichi.d/tool-output/) and NO dir/harness/"
fi

# --- 3: the pinned jichi + the random canaries (and a second dir's differ) ----
# A second rehearsal proves the canaries are random, not a fixed constant.
"$HBIN" init --config "$tmp/cfg.json" rehearsal \
    "$tmp/dir2" --from "$src" --jichi-bin "$tmp/jichi" \
    < /dev/null > "$tmp/i3b" 2>&1
rc2=$?
if [ ! -x "$dir/bin/jichi" ] || [ "$rc2" -ne 0 ] || \
   ! grep -qE '^JICHI_API_KEY=CANARY-[0-9a-f]{24}$' "$dir/canary.env" 2>/dev/null || \
   ! grep -qE '^tokcanary-[0-9a-f]{24}$' "$dir/canary.token" 2>/dev/null || \
   ! grep -qE '^JICHI_API_KEY=CANARY-[0-9a-f]{24}$' "$tmp/dir2/canary.env" 2>/dev/null; then
    t_fail "3: bin/jichi not pinned + executable, or canaries missing/wrong shape (rc2=$rc2):
canary.env: $(head_bytes 80 "$dir/canary.env" 2>/dev/null)
canary.token: $(head_bytes 80 "$dir/canary.token" 2>/dev/null)
dir2: $(head_bytes 80 "$tmp/dir2/canary.env" 2>/dev/null)"
else
    _c1=$(sed -n 's/^JICHI_API_KEY=//p' "$dir/canary.env")
    _t1=$(sed -n '1p' "$dir/canary.token")
    _c2=$(sed -n 's/^JICHI_API_KEY=//p' "$tmp/dir2/canary.env")
    _t2=$(sed -n '1p' "$tmp/dir2/canary.token")
    # smoke_lint check 5: this non-lint driver runs the tree's "$BIN" once.
    # The pinned-binary report (check 3's subject) carries whatever `build:`
    # line the binary answers; the tree's own jichi answers one in a git
    # checkout, so a harness that drifted from the tree would pin a different
    # stamp. Run it here; check 1's green report carries the fake's stamp.
    "$BIN" --version > "$tmp/jv" 2>/dev/null
    [ "$_c1" = "$_t1" ] && t_fail "3: the two canaries of ONE dir are identical -- they must be two independent draws"
    [ "$_c1" = "$_c2" ] && t_fail "3: the canary of a SECOND dir is identical to the first -- a fixed constant, not a random draw (a rehearsal's key must be useless outside its own directory)"
    t_ok "3: bin/jichi pinned + executable; canaries random (CANARY-<24hex> / tokcanary-<24hex>), the two differ, and a second dir drew different ones"
fi

# --- 4: standin.git -- bare, carrying the plan branch, a SINK with no origin --
# The reference (setup-dir.sh 72-76) makes standin.git the rehearsal remote itself
# (REMOTE=$DIR/standin.git): the source is pushed INTO it by path, and nothing
# points back at it with an origin. So the port must leave it origin-less.
if [ ! -d "$dir/standin.git" ] || \
   ! git -C "$dir/standin.git" rev-parse --is-bare-repository >/dev/null 2>&1 || \
   ! git -C "$dir/standin.git" rev-parse --verify -q refs/heads/harness-plan/plan >/dev/null 2>&1; then
    t_fail "4: standin.git is missing, not bare, or does not carry the source's plan branch (harness-plan/plan)"
else
    _so=$(git -C "$dir/standin.git" config remote.origin.url 2>/dev/null)
    if [ -z "$_so" ]; then
        t_ok "4: standin.git is bare, carries harness-plan/plan, and has NO origin (a sink the source is pushed into, never a real remote)"
    else
        t_fail "4: standin.git has an origin ($_so) -- the reference leaves it origin-less; it is a sink, not a client (setup-dir.sh 72-76)"
    fi
fi

# --- 5: team/ on <prefix>/main, dead origin, no *.sample hooks ----------------
if ! git -C "$dir/team" rev-parse --verify -q HEAD >/dev/null 2>&1; then
    t_fail "5: team/ was not cloned (no HEAD)"
else
    _tb=$(git -C "$dir/team" rev-parse --abbrev-ref HEAD 2>/dev/null)
    _to=$(git -C "$dir/team" remote get-url origin 2>/dev/null)
    _ns=$(ls "$dir/team/.git/hooks/"*.sample 2>/dev/null)
    if [ "$_tb" != "harness-plan/main" ] || [ "$_to" != "/nonexistent-the-harness-pushes" ] || [ -n "$_ns" ]; then
        t_fail "5: team/ on branch '$_tb' (want harness-plan/main), origin '$_to' (want /nonexistent-the-harness-pushes), sample hooks: $_ns"
    else
        t_ok "5: team/ on harness-plan/main, origin /nonexistent-the-harness-pushes (the team never fetches or pushes; the harness pushes from the mirror), no *.sample hooks"
    fi
fi

# --- 6: mirror.git -- bare, origin set, the fetch refspec, the fetch reported -
if [ ! -d "$dir/mirror.git" ] || \
   ! git -C "$dir/mirror.git" rev-parse --is-bare-repository >/dev/null 2>&1; then
    t_fail "6: mirror.git is missing or not bare"
else
    _mo=$(git -C "$dir/mirror.git" remote get-url origin 2>/dev/null)
    _mf=$(git -C "$dir/mirror.git" config remote.origin.fetch 2>/dev/null)
    if [ "$_mf" != "+refs/heads/*:refs/remotes/origin/*" ] || \
       ! grep -qE 'mirror\.git fetched [0-9]+ remote refs' "$tmp/i1"; then
        t_fail "6: mirror.git origin '$_mo', fetch refspec '$_mf' (want +refs/heads/*:refs/remotes/origin/*), init report: $(grep -E '^init ' "$tmp/i1" | head_bytes 300)"
    else
        t_ok "6: mirror.git bare, origin set, fetch refspec +refs/heads/*:refs/remotes/origin/*, and the report says it fetched (a numeric remote-ref count)"
    fi
fi

# --- 7: recs/ on <prefix>/records (orphan), README committed, origin set ------
if ! git -C "$dir/recs" rev-parse --verify -q HEAD >/dev/null 2>&1; then
    t_fail "7: recs/ was not initialised (no HEAD)"
else
    _rb=$(git -C "$dir/recs" rev-parse --abbrev-ref HEAD 2>/dev/null)
    _ro=$(git -C "$dir/recs" remote get-url origin 2>/dev/null)
    _rf=$(git -C "$dir/recs" log -1 --name-only --format= 2>/dev/null)
    if [ "$_rb" != "harness-plan/records" ] || [ -z "$_ro" ] || ! printf '%s\n' "$_rf" | grep -q '^README\.md$'; then
        t_fail "7: recs/ on branch '$_rb' (want harness-plan/records), origin '$_ro' (want set), first commit files: $_rf"
    else
        t_ok "7: recs/ on harness-plan/records (orphan), README.md committed, origin set ($_ro)"
    fi
fi

# --- 8: IDEMPOTENCE -- a second run changes nothing ----------------------------
_before=$( (cd "$dir" && find . | sort) )
_can_a=$(cat "$dir/canary.env" "$dir/canary.token" 2>/dev/null)
"$HBIN" init --config "$tmp/cfg.json" rehearsal \
    "$dir" --from "$src" --jichi-bin "$tmp/jichi" \
    < /dev/null > "$tmp/i8" 2> "$tmp/i8.err"
rc8=$?
_after=$( (cd "$dir" && find . | sort) )
_can_b=$(cat "$dir/canary.env" "$dir/canary.token" 2>/dev/null)
if [ "$rc8" -ne 0 ] || [ "$_can_a" != "$_can_b" ] || [ "$_before" != "$_after" ]; then
    t_fail "8: second init: rc=$rc8, canaries changed=$([ "$_can_a" = "$_can_b" ] || echo yes), tree changed=$([ "$_before" = "$_after" ] || echo yes) -- idempotence (the core of H2) failed: $(head_bytes 200 "$tmp/i8.err")"
else
    _lo=0
    for part in "bin/jichi" "team/" "mirror\.git" "recs/"; do
        grep -qE "$part.*left as it is" "$tmp/i8" && _lo=$((_lo + 1))
    done
    if [ "$_lo" -ne 4 ]; then
        t_fail "8: the second run reported only $_lo of 4 'left as it is' lines (want bin/jichi, team/, mirror.git, recs/):
$(grep -E 'left as it is' "$tmp/i8" | head_bytes 300)"
    else
        t_ok "8: IDEMPOTENCE -- second run exits 0, 'left as it is' for bin/jichi, team/, mirror.git, recs/; canaries unchanged; nothing deleted"
    fi
fi

# --- 9: the usage refusal precedes the layout ----------------------------------
"$HBIN" init bogusmode "$tmp/dir9" --config "$tmp/cfg.json" \
    < /dev/null > "$tmp/i9" 2> "$tmp/i9.err"
rc9=$?
if [ "$rc9" -ne 2 ] || ! grep -qF "MODE must be drive or rehearsal" "$tmp/i9.err"; then
    t_fail "9: \`init bogusmode <dir>\` exited $rc9 (want 2) and/or does not say 'MODE must be drive or rehearsal' on stderr: $(head_bytes 200 "$tmp/i9.err")"
else
    if [ -e "$tmp/dir9" ]; then
        t_fail "9: the usage refusal CREATED $tmp/dir9 -- a refusal happens BEFORE any part is written (the never-delete rule, and no silent success)"
    else
        t_ok "9: \`init <bogus> <dir>\` exits 2 with 'MODE must be drive or rehearsal' and creates nothing (the refusal precedes the layout)"
    fi
fi

# --- 10: the drive key-file indirection (offline, canary-tested) ---------------
cat > "$tmp/dcfg.json" <<EOF
{"dir":"$tmp/d2",
"drive":{"branchPrefix":"harness-plan","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"modelKeyFile":"$tmp/kreal","gitTokenFile":"$tmp/tokfile"}
EOF
cat > "$tmp/dcfg-nok.json" <<EOF
{"dir":"$tmp/d3",
"drive":{"branchPrefix":"harness-plan","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"gitTokenFile":"$tmp/tokfile"}
EOF
kreal_bytes=$(cat "$tmp/kreal")
"$HBIN" init --config "$tmp/dcfg.json" drive \
    "$tmp/d2" --from "$src" --jichi-bin "$tmp/jichi" \
    < /dev/null > "$tmp/i10" 2> "$tmp/i10.err"
rc10=$?
ok10=1
if [ "$rc10" -ne 0 ]; then
    ok10=0; _e="drive init exited $rc10: $(head_bytes 160 "$tmp/i10.err")"
elif ! grep -qF "JICHI_API_KEY=\$(tr -d ' \t\n\r' < $tmp/kreal)" "$tmp/d2/key.env" 2>/dev/null; then
    ok10=0; _e="key.env lacks the reference line (it is a REFERENCE, not a copy): $(head_bytes 160 "$tmp/d2/key.env" 2>/dev/null)"
elif [ "$(ls -l "$tmp/d2/key.env" | cut -c1-10)" != "-rw-------" ]; then
    ok10=0; _e="key.env mode is $(ls -l "$tmp/d2/key.env" | cut -c1-10), want -rw------- (600)"
elif printf '%s' "$(cat "$tmp/d2/key.env")" | grep -qF "$kreal_bytes"; then
    ok10=0; _e="the canary (the key file's CONTENTS) appears in key.env's bytes -- no copy is ever written"
fi
[ "$ok10" -eq 1 ] || {
    t_fail "10: the drive key-file indirection failed -- $_e"
    t_done
}
# A drive config with a MISSING model-key reference: a NAMED refusal, exit 2,
# before the layout.
"$HBIN" init --config "$tmp/dcfg-nok.json" drive \
    "$tmp/d3" --from "$src" --jichi-bin "$tmp/jichi" \
    < /dev/null > "$tmp/i10b" 2> "$tmp/i10b.err"
rc10b=$?
if [ "$rc10b" -ne 2 ] || ! grep -qF "JICHI_HARNESS_MODEL_KEY_FILE" "$tmp/i10b.err"; then
    t_fail "10b: the drive config with a missing model-key reference exited $rc10b (want 2) and/or does not name JICHI_HARNESS_MODEL_KEY_FILE: $(head_bytes 160 "$tmp/i10b.err")"
    t_done
fi
t_ok "10: the drive key.env carries the REFERENCE line (mode 600, the canary absent from its bytes); a missing model-key reference exits 2 naming JICHI_HARNESS_MODEL_KEY_FILE"

# --- 11: build_stamp survives a final line without a trailing newline (S1) ----
# (a) the 'build:' line with a final line following it: still parsed.
# (b) no 'build:' line at all: the walk must stop at the NUL; the unfixed
#     `p = strchr(p, '\n') + 1` reads one byte past the end and SEGFAULTS.
printf '#!/bin/sh\nif [ "$1" = "--version" ]; then\n  printf "jichi 0.0.0-fake\\nbuild: nlstamp\\nlast no newline"\nfi\nexit 0\n' > "$tmp/jichi-nl"
chmod 755 "$tmp/jichi-nl"
printf '#!/bin/sh\nif [ "$1" = "--version" ]; then\n  printf "jichi 0.0.0-fake\\nno build line, final no newline"\nfi\nexit 0\n' > "$tmp/jichi-nl-nob"
chmod 755 "$tmp/jichi-nl-nob"
"$HBIN" init --config "$tmp/cfg.json" rehearsal \
    "$tmp/di11a" --from "$src" --jichi-bin "$tmp/jichi-nl" \
    < /dev/null > "$tmp/i11a" 2> "$tmp/i11a.err"
rc11a=$?
if [ "$rc11a" -eq 0 ] && grep -q "nlstamp" "$tmp/i11a"; then
    _11a=1
else
    _11a=0
    t_fail "11: S1 (build_stamp advanced with p = strchr(p, '\n') + 1, which is undefined past the last line): a final line with NO trailing newline must not break the 'build:' parse; part (a) (a build: line FOLLOWED by a final line without a newline) exited $rc11a: $(head_bytes 300 "$tmp/i11a" "$tmp/i11a.err")"
fi
if [ "$_11a" -eq 1 ]; then
    "$HBIN" init --config "$tmp/cfg.json" rehearsal \
        "$tmp/di11b" --from "$src" --jichi-bin "$tmp/jichi-nl-nob" \
        < /dev/null > "$tmp/i11b" 2> "$tmp/i11b.err"
    rc11b=$?
    if [ "$rc11b" -ne 0 ]; then
        t_fail "11: S1 (build_stamp advanced with p = strchr(p, '\n') + 1, which is undefined past the last line): with no 'build:' line and a final line WITHOUT a trailing newline, init exited $rc11b (expected 0): $(head_bytes 300 "$tmp/i11b" "$tmp/i11b.err")"
    else
        t_ok "11: a pinned binary whose --version --verbose ends with a final line WITHOUT a trailing newline: (a) the build: line is still parsed, (b) without a build: line init exits 0 (no crash)"
    fi
fi

# --- 12: a non-executable pin is re-pinned (S2) --------------------------------
# The reference pins with [ -x ] (setup-dir.sh line 61), not [ -e ]: a pin that
# exists but lost its executable bit must be re-pinned, not left as it is.
# Fixture: a full first init into dir12, then strip the pin's exec bit, then
# the second init.
"$HBIN" init --config "$tmp/cfg.json" rehearsal \
    "$tmp/dir12" --from "$src" --jichi-bin "$tmp/jichi" \
    < /dev/null > "$tmp/i12s" 2>&1
rc12s=$?
if [ "$rc12s" -ne 0 ]; then
    t_fail "12: S2 (the pin check is jc_file_exists; the reference tests -x, not existence): the fixture's first init exited $rc12s: $(head_bytes 300 "$tmp/i12s")"
else
    chmod 644 "$tmp/dir12/bin/jichi"
    "$HBIN" init --config "$tmp/cfg.json" rehearsal \
        "$tmp/dir12" --from "$src" --jichi-bin "$tmp/jichi" \
        < /dev/null > "$tmp/i12" 2> "$tmp/i12.err"
    rc12=$?
    if [ "$rc12" -ne 0 ]; then
        t_fail "12: S2 (the pin check is jc_file_exists; the reference tests -x, not existence): a second init into a dir whose bin/jichi exists but is mode 644 exited $rc12 (want 0): $(head_bytes 300 "$tmp/i12" "$tmp/i12.err")"
    elif [ ! -x "$tmp/dir12/bin/jichi" ]; then
        t_fail "12: S2 (the pin check is jc_file_exists; the reference tests -x, not existence): the mode-644 pin was NOT re-pinned -- the report said '$(grep -F 'bin/jichi' "$tmp/i12" | head -n 1)' and bin/jichi is still not executable: $(tr '\n' ' ' < "$tmp/i12" | head_bytes 200)"
    elif ! grep -qF "re-pinned (it existed but was not executable)" "$tmp/i12"; then
        t_fail "12: S2 (the pin check is jc_file_exists; the reference tests -x, not existence): the re-pin is not named in the report: $(grep -F 'bin/jichi' "$tmp/i12" | tr '\n' ' ' | head_bytes 200)"
    else
        t_ok "12: a pin that exists but is mode 644 is re-pinned (executable again) and the report says 're-pinned (it existed but was not executable)'"
    fi
fi

# --- 13: an over-long drive.branchPrefix is a usage refusal BEFORE the layout (S3) --
# 200 chars + '/records' cannot fit the branch-name buffer: init must refuse
# with exit 2 naming branchPrefix, and the refusal must precede the layout.
_pb="abcdef"
while [ "${#_pb}" -lt 200 ]; do _pb="$_pb$_pb"; done
_p200=$(printf '%s' "$_pb" | head_bytes 200)
cat > "$tmp/cfg-long.json" <<EOF
{"dir":"$tmp/di13",
"drive":{"branchPrefix":"$_p200","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"modelKeyFile":"$tmp/kreal","gitTokenFile":"$tmp/tokfile"}
EOF
"$HBIN" init --config "$tmp/cfg-long.json" rehearsal \
    "$tmp/di13" --from "$src" --jichi-bin "$tmp/jichi" \
    < /dev/null > "$tmp/i13" 2> "$tmp/i13.err"
rc13=$?
if [ "$rc13" -ne 2 ]; then
    t_fail "13: S3 (an over-long drive.branchPrefix must be a usage-class refusal BEFORE the layout): init with a 200-char branchPrefix exited $rc13 (want 2) and LAYED OUT parts (runs/ present: $([ -d "$tmp/di13/runs" ] && echo yes || echo no)): $(tr '\n' ' ' < "$tmp/i13" | head_bytes 200)"
elif ! grep -qF "branchPrefix" "$tmp/i13.err"; then
    t_fail "13: S3 (an over-long drive.branchPrefix must be a usage-class refusal BEFORE the layout): stderr does not name branchPrefix: $(tr '\n' ' ' < "$tmp/i13.err" | head_bytes 200)"
else
    _l13=0
    [ ! -d "$tmp/di13/runs" ] || _l13=1
    [ ! -d "$tmp/di13/team" ] || _l13=1
    [ ! -d "$tmp/di13/mirror.git" ] || _l13=1
    [ -z "$(ls -A "$tmp/di13" 2>/dev/null)" ] || _l13=1
    if [ "$_l13" -ne 0 ]; then
        t_fail "13: S3 (an over-long drive.branchPrefix must be a usage-class refusal BEFORE the layout): the refusal LAYED OUT PARTS of $tmp/di13: $(ls "$tmp/di13" 2>/dev/null | head_bytes 300)"
    else
        t_ok "13: a 200-char drive.branchPrefix exits 2 (usage class) naming branchPrefix and lays out NOTHING (no runs/, no team/, no mirror.git)"
    fi
fi

t_done
