#!/bin/sh
# smoke: `jichi-harness push records --config <cfg> <recs-dir>` -- the records
# half of the host push (M1147, batch H7; the C89 port of the 2026-10-27
# hardening-host drive's push.sh steps 5-8, def-360 retired). The recs repo is
# the init-built one (a plain non-bare repo, orphan branch <prefix>/records,
# origin = the host's records remote); the push reads it directly with the
# M2-neutral git child, runs the KEY SCAN (never the size check -- "the
# records are large by design", push.sh L20-21), and pushes by the stored
# absolute URL, never --force.
#
# M1147 (H7). Design note (the contract, the behaviour list B1):
#   docs/internal/drive/2026-11-05-harness-port/notes/M1147.md
# Ported reference (read line by line; this driver holds B1):
#   docs/internal/drive/2026-10-27-hardening-host/push.sh
#
# THE RED THIS IS HELD BY. On the tree before this batch (W1's skeleton) the
# top-level verb `push` EXISTS and is dispatched, but its five subcommand
# bodies are not wired yet: every `push records` run gets
# `jichi-harness: push records: not wired yet (M1147, batch H7)` on stderr
# and exit 2, and the ref landings / log lines the cells assert are absent.
# Cells 1-5 run a wired `push records` and are RED for exactly that reason
# (cell 6 gets exit 2 from the `push <sub>`-without---config dispatch too,
# but for the WRONG reason -- the green refusal must name the missing
# config, not the not-wired body, so its stderr assertion separates them).
#
# WHAT IS CHECKED -- 7 cells, one t_ok/t_fail each:
#   1. the happy path: a commit in recs is pushed to the local bare origin --
#      exit 0, refs/heads/<prefix>/records appears on the origin and its tip
#      equals recs' tip.
#   2. the key scan refuses: a commit whose blob carries the canary token is
#      NOT pushed -- exit 1, the offending file PATH is named on stderr, the
#      canary itself is NOT printed (the secret rule), the origin unchanged.
#   3. NO size check: a 9 MiB blob in recs IS pushed (exit 0, the ref moves
#      past it) -- the team push (`mirror push`) would refuse it at its 8 MiB
#      per-file limit; the absence of the size gate is the records contract
#      (push.sh L20-21), and this cell is its tooth.
#   4. rc-neutral remote fetch: the origin holds NO records branch yet; the
#      push still lands and exits 0 (the remote-fetch failure is rc-neutral,
#      push.sh L75-76 -- the orphan branch may not exist yet).
#   5. never --force: the origin's records ref is pre-set to an UNRELATED
#      commit (diverged history); the push is refused exit 1 (non-FF), the
#      origin's ref unchanged.
#   6. `push records` without --config is refused exit 2, naming the missing
#      config (not the missing verb -- the red's exit is 2 from a different
#      line, and the assertion reads the line, not the number).
#   7. the key scan counts BLOBS read, not every OID the range named
#      (M1155, SV:0438): a fresh recs clone + fresh bare origin whose records
#      branch holds a known nested-tree commit (3 blob + 3 tree + 1 commit).
#      The port must print "the key scan: 3 file version(s) checked, 0 with a
#      secret" -- the 3 BLOB versions it read (push.sh's pre-push key scan
#      counts blobs), NOT its named-OID count (6: the 3 blobs PLUS the 3
#      interior trees; the range's commit line is dropped by the OID parser).
#
# THE FIXTURE. Per the driver, all under $SMOKE_TMP: origin.git (a LOCAL
# bare repo, the stand-in for the host's records remote -- no network), and
# recs/ (a non-bare repo on the orphan branch drive/x-test/records whose
# origin is origin.git). The stored config copy
# (recs.git-config.harness-copy, init's M1147 B1.1) is written by the driver
# here, because the port's init extension is the sibling batch's work and the
# driver must not depend on it being green: the URL guard's reference is a
# file whose bytes the driver controls.
# The config JSON is a minimal harness config (dir, modelKeyFile, gitTokenFile,
# drive.branchPrefix, the note's host block) and is parsed by the loader
# point-wise: the host block is unknown to the red tree's loader and ignored
# there, which is why the red lands on `unknown command 'push'` and not on a
# config refusal. modelKeyFile carries a random hex canary; gitTokenFile
# carries a DISTINCT fake 40-hex canary (never a real key -- a leaked canary
# can be attributed to this driver by its name).
#
# Runs the tree's binary by path ($HBIN, the third_program idiom), never a
# PATH-resolved jichi-harness, and -- for smoke_lint check 5 -- also runs the
# tree's "$BIN" once. No timeout wrapper (the pushes are local and fast);
# git children carry -c user.name/-c user.email (M2-neutral).
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"

t_plan 7
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
TOKEN_CANARY="7f3a9c1e5d2b8a4f0c6e9a3d7b5f1e8a2c4d6f0b"
KEYCANARY=$(od -An -N20 -tx1 /dev/urandom | tr -d ' \n'); [ -n "$KEYCANARY" ] || KEYCANARY="canarykey$$_"

# A fixture failure emits one t_fail per cell so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt 7 ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

if [ ! -x "$HBIN" ]; then
    fixture_fail "the harness binary $HBIN is not built (run `make jichi-harness` first)"
fi

# --- the fixture: a local bare origin + the recs repo on its orphan branch ---
git init -q --bare "$tmp/origin.git" || fixture_fail "init origin.git"
git init -q "$tmp/recs" || fixture_fail "init recs"
git -C "$tmp/recs" checkout -q --orphan "$PFX/records" || fixture_fail "orphan branch"
printf 'records: the drive of 2026-11-05 harness port (smoke fixture)\n' > "$tmp/recs/README.md"
git -C "$tmp/recs" add README.md
git -C "$tmp/recs" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "records: the branch" || fixture_fail "initial records commit"
git -C "$tmp/recs" remote add origin "$tmp/origin.git" || fixture_fail "recs origin"

# The stored config copy (B1.1): the URL guard's reference, the bytes init
# will write once its extension lands; the driver writes them now.
printf '[remote "origin"]\n\turl = %s\n\tfetch = +refs/heads/*:refs/remotes/origin/*\n' \
    "$tmp/origin.git" > "$tmp/recs.git-config.harness-copy"

printf '%s\n' "$KEYCANARY" > "$tmp/kfile"; chmod 600 "$tmp/kfile"
printf '%s\n' "$TOKEN_CANARY" > "$tmp/tokfile"; chmod 600 "$tmp/tokfile"

cat > "$tmp/cfg.json" <<EOF
{"dir":"$tmp",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"drive":{"branchPrefix":"$PFX","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"host":{"pushHome":"$tmp/pushhome","image":"jichi-drive:test","pageCommand":"$tmp/page.sh"}}
EOF

# tip REFSPEC helper: the full ref the records push must land.
RREF="refs/heads/$PFX/records"

# --- 1: the happy path (a commit in recs lands on the bare origin) ----------
git -C "$tmp/recs" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "records: snapshot one" || fixture_fail "c1 commit"
c1tip=$(git -C "$tmp/recs" rev-parse HEAD) || fixture_fail "c1 rev-parse"
"$HBIN" push records --config "$tmp/cfg.json" "$tmp/recs" \
    < /dev/null > "$tmp/p1" 2> "$tmp/p1.err"
rc1=$?
o1=$(git -C "$tmp/origin.git" rev-parse "$RREF" 2>/dev/null)
if [ "$rc1" -ne 0 ]; then
    t_fail "1: the happy records push exited $rc1 (want 0) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/p1.err" | head_bytes 200)"
elif [ -z "$o1" ]; then
    t_fail "1: the records push exited 0 but the origin holds no $RREF -- nothing was pushed"
elif [ "$o1" != "$c1tip" ]; then
    t_fail "1: the origin's $RREF is $o1, not recs' tip $c1tip -- the landing is wrong"
else
    t_ok "1: the happy path -- a commit in recs is pushed to the local bare origin, exit 0, the ref appears and its tip matches recs' tip"
fi

# --- 2: the key scan refuses (canary in a blob the push adds) ----------------
# A SEPARATE recs copy (recs2, a clone of recs): the canary-bearing commit
# lives only there, so the main recs history (cells 1, 3, 4, 5) stays
# canary-free -- a scan range that crossed cell 2's commit would refuse
# every later push.
git clone -q "$tmp/recs" "$tmp/recs2" || fixture_fail "c2 clone recs2"
printf '[remote "origin"]\n\turl = %s\n\tfetch = +refs/heads/*:refs/remotes/origin/*\n' \
    "$tmp/origin.git" > "$tmp/recs2.git-config.harness-copy"
git -C "$tmp/recs2" checkout -q "$PFX/records" || fixture_fail "c2 checkout"
printf 'secret=%s\n' "$TOKEN_CANARY" > "$tmp/recs2/leak.txt"
git -C "$tmp/recs2" add leak.txt
git -C "$tmp/recs2" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "records: a canary-bearing snapshot" || fixture_fail "c2 commit"
c2before=$(git -C "$tmp/origin.git" rev-parse "$RREF" 2>/dev/null)
"$HBIN" push records --config "$tmp/cfg.json" "$tmp/recs2" \
    < /dev/null > "$tmp/p2" 2> "$tmp/p2.err"
rc2=$?
c2after=$(git -C "$tmp/origin.git" rev-parse "$RREF" 2>/dev/null)
if [ "$rc2" -ne 1 ]; then
    t_fail "2: the canary-bearing records push was not refused (exit $rc2, want 1) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/p2.err" | head_bytes 200)"
elif ! grep -qF "leak.txt" "$tmp/p2" "$tmp/p2.err"; then
    t_fail "2: the key scan refused but does not name the offending file PATH (leak.txt) on stderr: $(tr '\n' ' ' < "$tmp/p2.err" | head_bytes 200)"
elif grep -qF "$TOKEN_CANARY" "$tmp/p2" "$tmp/p2.err"; then
    t_fail "2: the key scan REFUSED but printed the canary CONTENT (only the path may be named, the secret rule)"
elif [ "$c2before" != "$c2after" ]; then
    t_fail "2: the key scan refused but the origin's $RREF MOVED ($c2before -> $c2after) -- the refusal must happen before any push"
else
    t_ok "2: the key scan refuses a commit whose blob carries the canary token -- exit 1, the path named on stderr, the token itself never printed, the origin unchanged"
fi

# --- 3: NO size check (a 9 MiB blob in records IS pushed) -------------------
# The team push's size gate (8 MiB per file version) would refuse this; the
# records push has no size check (push.sh L20-21: "the records are large by
# design"). A 9 MiB blob must LAND.
dd if=/dev/urandom of="$tmp/recs/big9.bin" bs=1048576 count=9 2>/dev/null \
    || fixture_fail "c3 dd"
git -C "$tmp/recs" add big9.bin
git -C "$tmp/recs" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "records: a 9 MiB snapshot" || fixture_fail "c3 commit"
c3tip=$(git -C "$tmp/recs" rev-parse HEAD) || fixture_fail "c3 rev-parse"
"$HBIN" push records --config "$tmp/cfg.json" "$tmp/recs" \
    < /dev/null > "$tmp/p3" 2> "$tmp/p3.err"
rc3=$?
o3=$(git -C "$tmp/origin.git" rev-parse "$RREF" 2>/dev/null)
if [ "$rc3" -ne 0 ]; then
    t_fail "3: the 9 MiB records push was refused (exit $rc3, want 0) -- the records contract has NO size check (the team push would refuse it at 8 MiB per file): $(tr '\n' ' ' < "$tmp/p3.err" | head_bytes 200)"
elif [ -z "$o3" ] || [ "$o3" != "$c3tip" ]; then
    t_fail "3: the 9 MiB push exited 0 but the origin's $RREF ($o3) does not land at recs' tip ($c3tip)"
else
    t_ok "3: no size check -- a 9 MiB blob in records IS pushed (exit 0, the ref moves past it); the team push would refuse it, the records contract does not"
fi

# --- 4: rc-neutral remote fetch (the origin has no records branch yet) ------
# A FRESH origin: the remote holds none of the branch, so its fetch fails
# (rc-neutral, push.sh L75-76: "the orphan branch may not exist yet"). The
# scan then reads the whole (orphan) branch and the push still lands, exit 0.
git init -q --bare "$tmp/origin4.git" || fixture_fail "c4 init origin"
git -C "$tmp/recs" remote set-url origin "$tmp/origin4.git" || fixture_fail "c4 set-url"
printf '[remote "origin"]\n\turl = %s\n\tfetch = +refs/heads/*:refs/remotes/origin/*\n' \
    "$tmp/origin4.git" > "$tmp/recs.git-config.harness-copy"
c4tip=$(git -C "$tmp/recs" rev-parse HEAD) || fixture_fail "c4 rev-parse"
# --verify: a missing ref stays on stderr (a bare rev-parse PRINTS the ref
# name to stdout when it fails -- the emptiness check would see non-empty).
git -C "$tmp/origin4.git" rev-parse --verify -q "$RREF" >/dev/null 2>&1 && \
    fixture_fail "c4 origin already holds $RREF"
"$HBIN" push records --config "$tmp/cfg.json" "$tmp/recs" \
    < /dev/null > "$tmp/p4" 2> "$tmp/p4.err"
rc4=$?
o4=$(git -C "$tmp/origin4.git" rev-parse "$RREF" 2>/dev/null)
if [ "$rc4" -ne 0 ]; then
    t_fail "4: the push to an origin without the records branch exited $rc4 (want 0) -- the remote-fetch failure is rc-NEUTRAL (push.sh L75-76): $(tr '\n' ' ' < "$tmp/p4.err" | head_bytes 200)"
elif [ "$o4" != "$c4tip" ]; then
    t_fail "4: the rc-neutral push exited 0 but the fresh origin's $RREF ($o4) does not equal recs' tip ($c4tip)"
else
    t_ok "4: rc-neutral remote fetch -- with the origin holding no records branch yet, the push still lands and exits 0 (the missing-branch fetch is not a failure)"
fi

# --- 5: never --force (a diverged origin ref is refused, unchanged) ---------
# A fresh clone (recs5) whose origin is the MAIN origin.git: its records ref
# is pre-set to an UNRELATED commit (independent history, no common ancestor
# with recs5's tip), so the push is a non-fast-forward and must be refused
# exit 1, without --force, origin's ref unchanged. (The driver lands the
# unrelated commit by pushing it to the origin's ref directly -- the fixture
# is the driver's instrument; the port under test is what must never do that.)
git clone -q "$tmp/recs" "$tmp/recs5" || fixture_fail "c5 clone recs5"
printf '[remote "origin"]\n\turl = %s\n\tfetch = +refs/heads/*:refs/remotes/origin/*\n' \
    "$tmp/origin.git" > "$tmp/recs5.git-config.harness-copy"
git -C "$tmp/recs5" checkout -q "$PFX/records" || fixture_fail "c5 checkout"
_side="$tmp/side"
git init -q "$_side" || fixture_fail "c5 init side"
git -C "$_side" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "an unrelated line of history" || fixture_fail "c5 side commit"
div=$(git -C "$_side" rev-parse HEAD) || fixture_fail "c5 side rev-parse"
git -C "$_side" push -q --force "$tmp/origin.git" "HEAD:$RREF" \
    < /dev/null > "$tmp/p5seed" 2>&1 || \
    fixture_fail "c5 seed origin: $(tr '\n' ' ' < "$tmp/p5seed" | head_bytes 160)"
c5before=$(git -C "$tmp/origin.git" rev-parse "$RREF")
[ "$c5before" = "$div" ] || fixture_fail "c5 origin seed did not land at $div"
"$HBIN" push records --config "$tmp/cfg.json" "$tmp/recs5" \
    < /dev/null > "$tmp/p5" 2> "$tmp/p5.err"
rc5=$?
c5after=$(git -C "$tmp/origin.git" rev-parse "$RREF" 2>/dev/null)
if [ "$rc5" -ne 1 ]; then
    t_fail "5: the non-FF records push (origin's $RREF pre-set to an unrelated commit) exited $rc5 (want 1) -- never --force: $(tr '\n' ' ' < "$tmp/p5.err" | head_bytes 200)"
elif [ "$c5before" != "$c5after" ]; then
    t_fail "5: the non-FF refusal CHANGED the origin's $RREF ($c5before -> $c5after) -- the push must be refused, not forced"
else
    t_ok "5: never --force -- the origin's records ref pre-set to an unrelated commit makes the push a non-FF refusal (exit 1), and the origin's ref is unchanged"
fi

# --- 6: `push records` without --config is refused exit 2 (config named) ----
# The red tree answers this cell with exit 2 too -- but from
# `unknown command 'push'`, a different line. The assertion reads the line:
# the green refusal must NAME the missing config, and must NOT be the
# unknown-command line (a port that dispatches the verb but forgets the
# config requirement is red here; so is the red tree).
"$HBIN" push records "$tmp/recs" \
    < /dev/null > "$tmp/p6" 2> "$tmp/p6.err"
rc6=$?
if [ "$rc6" -ne 2 ]; then
    t_fail "6: \`push records\` without --config exited $rc6 (want 2): $(tr '\n' ' ' < "$tmp/p6.err" | head_bytes 200)"
elif ! grep -qiE "config" "$tmp/p6.err"; then
    t_fail "6: the refusal does not name the missing config (on the red tree the line is \`unknown command 'push'\`, not the config requirement): $(tr '\n' ' ' < "$tmp/p6.err" | head_bytes 200)"
else
    t_ok "6: \`push records\` without --config is refused exit 2, naming the missing config (not the missing verb -- that is the red's own line)"
fi

# --- 7: the key scan counts BLOBS read, not every OID the range named --------
# M1155 (SV:0438): the records key scan printed `n` (the export's OID list --
# blobs + trees + the commit) as the "file version(s) checked" count, but it
# only READ the blobs (the commit/tree OIDs are `continue`d past). push.sh's
# pre-push key scan counts blobs, so the port diverged from the reference by
# exactly the number of interior trees in the range. This cell pins it.
#
# Fully INDEPENDENT of cells 1-6 (they mutate the MAIN recs origin): a fresh
# bare origin7.git + a fresh recs clone (recs7, origin = origin7). The clone
# carries the tracking ref refs/remotes/origin/<PFX>/records at recs' tip, so
# the scan's range (the tip, minus the remote's current tips, minus the
# tracking refs) is EXACTLY the new nested commit's own objects -- no
# dependence on cells 1-6's origin.
#
# The new commit holds EXACTLY three files with nested trees: README.md
# (top-level), a.txt (top-level), runs/0001/a.jsonl (one directory deep) ->
# 3 blob + 3 tree (/, runs, runs/0001) + 1 commit. The fresh origin7 holds
# nothing, so nothing is excluded.
#
# MEASURED counts (the red's tooth). The port's hp_range_shas counts a
# rev-list --objects line only when its 41st byte is a space or EOL; the
# commit line is BARE (no path, EOL at 40), so it is dropped. Hence the
# UNFIXED port's `n` = 3 trees + 3 blobs = 6, and it prints 6. The FIXED
# port counts the blobs it actually READ (3) and prints 3. The tree-inflation
# is the defect: 3 blobs are read but 6 are reported, diverging from
# push.sh's blob count by the 3 interior trees.
git init -q --bare "$tmp/origin7.git" || fixture_fail "c7 init origin7"
git clone -q "$tmp/recs" "$tmp/recs7" || fixture_fail "c7 clone recs7"
printf '[remote "origin"]\n\turl = %s\n\tfetch = +refs/heads/*:refs/remotes/origin/*\n' \
    "$tmp/origin7.git" > "$tmp/recs7.git-config.harness-copy"
git -C "$tmp/recs7" checkout -q "$PFX/records" || fixture_fail "c7 checkout"
# Reset the branch to the known nested topology: empty the index and working
# tree, add exactly the three files, commit.
git -C "$tmp/recs7" rm -rq --cached . 2>/dev/null || fixture_fail "c7 unstage"
find "$tmp/recs7" -mindepth 1 -maxdepth 1 -not -name .git -exec rm -rf {} + 2>/dev/null
printf 'README seven (nested-tree count fixture)\n' > "$tmp/recs7/README.md"
printf 'a.txt seven\n' > "$tmp/recs7/a.txt"
mkdir -p "$tmp/recs7/runs/0001" || fixture_fail "c7 mkdir"
printf '{"tick": 7}\n' > "$tmp/recs7/runs/0001/a.jsonl"
git -C "$tmp/recs7" add README.md a.txt runs/0001/a.jsonl || fixture_fail "c7 add"
git -C "$tmp/recs7" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "records: a nested-tree snapshot (M1155)" || fixture_fail "c7 commit"
c7tip=$(git -C "$tmp/recs7" rev-parse HEAD) || fixture_fail "c7 rev-parse"
# Sanity: the range really is the known topology (the clone's tracking ref
# pins recs' tip, so only the new commit's objects are in the range). The
# range holds 7 objects (3 blob + 3 tree + 1 commit); the port's hp_range_shas
# drops the bare commit line, so its `n` = 6; the blobs it must READ = 3.
_c7range=$(git -C "$tmp/recs7" rev-list --objects "$c7tip" --not --remotes=origin 2>/dev/null)
_c7nobj=$(printf '%s\n' "$_c7range" | grep -cE '^[0-9a-f]{40}')
_c7nport=$(printf '%s\n' "$_c7range" | grep -cE '^[0-9a-f]{40} ')
_c7nblob=$(printf '%s\n' "$_c7range" | awk '{print $1}' | git -C "$tmp/recs7" cat-file --batch-check='%(objecttype)' 2>/dev/null | grep -c '^blob$')
if [ "$_c7nobj" -ne 7 ] || [ "$_c7nport" -ne 6 ] || [ "$_c7nblob" -ne 3 ]; then
    t_fail "7: the fixture range is not the known topology (range objects $_c7nobj, port-n $_c7nport, blobs $_c7nblob; want 7 / 6 / 3) -- the cell cannot pin the count"
else
    "$HBIN" push records --config "$tmp/cfg.json" "$tmp/recs7" \
        < /dev/null > "$tmp/p7" 2> "$tmp/p7.err"
    rc7=$?
    o7=$(git -C "$tmp/origin7.git" rev-parse "$RREF" 2>/dev/null)
    _c7countline=$(grep -hE 'the key scan: [0-9]+ file version\(s\) checked' "$tmp/p7" "$tmp/p7.err" 2>/dev/null | head_bytes 200)
    if [ "$rc7" -ne 0 ]; then
        t_fail "7: the nested-tree records push exited $rc7 (want 0): $(tr '\n' ' ' < "$tmp/p7.err" | head_bytes 200)"
    elif [ "$o7" != "$c7tip" ]; then
        t_fail "7: the push exited 0 but the fresh origin7's $RREF ($o7) does not land at recs7's tip ($c7tip)"
    elif ! grep -qF 'the key scan: 3 file version(s) checked, 0 with a secret' "$tmp/p7" "$tmp/p7.err"; then
        t_fail "7: the key scan printed the WRONG file-version count (it must count the BLOBS it read -- 3 -- not the port's named-OID count -- 6, which includes the 3 interior trees; the unfixed port prints 6); the clean line read: ${_c7countline:-<none>} (want \"the key scan: 3 file version(s) checked, 0 with a secret\")"
    else
        t_ok "7: the key scan counts the BLOBS it read, not the named OIDs the range carries -- the nested-tree commit (3 blob + 3 tree + 1 commit; the port's n=6 incl. the 3 trees) prints exactly \"the key scan: 3 file version(s) checked, 0 with a secret\" (M1155, SV:0438)"
    fi
fi

t_done
