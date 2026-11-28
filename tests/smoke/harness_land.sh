#!/bin/sh
# smoke: `jichi-harness land --config <f> --push-repo <d> --ci-log-dir <d>
# --gate-home <d> --main-checkout <d> --land-log <f> <SHA8>` -- the landing
# policy (M1202, batch H10; the C89 port of the 2026-10-27 hardening-host
# drive's land.sh). Moves master to a SHA8 by a credential-wired fast-forward
# push, under the standing rule (a make ci record with rc=0, a 'ci: OK' line,
# a green sweep verdict, the sha on the team branch, master a proper ancestor
# re-read by ls-remote) and the re-read verdict.
#
# M1202 (H10). Design note (the contract, the behaviour list b1-b16, the
# cells, the CHANGED table):
#   docs/internal/drive/2026-11-05-harness-port/notes/M1202.md
# Ported reference (read line by line; every refusal is kept BYTE-FOR-BYTE):
#   docs/internal/drive/2026-10-27-hardening-host/land.sh
#
# WHAT IS CHECKED -- 18 cells, one t_ok/t_fail each. Wave 1 held cells 1-12
# (cells 1-11 and 13 of the note; cell 12 -- the D1/D3 policy/orchestrator
# half -- is wave 2b's). Wave 2a adds the six byte-fidelity teeth cells
# 13-18 (the H10 wave-1 review's F1/F2/F3 red-first arms + the F10 b15 and
# the A5 b12-empty-M2 arms, the last two teeth existing arms) and extends
# cells 6/9/10/11 (F9/F8/F6/b10-wording).
#   1. b1 usage: no <SHA8> -> rc 2, the stderr names `usage: jichi-harness land <SHA8>`.
#   2. b2: the sha is not in push.git -> the named refusal, rc 1, and NO ci
#      record read (the refusal precedes b3: run.log empty, the ci reads left
#      no side effect).
#   3. b3/b4: the ci-<s8>.log.meta's LAST line ends ` rc=1` -> b3's named
#      refusal; then a ` rc=0` record whose ci-<s8>.log has NO `^ci: OK` line
#      -> b4's named refusal (two refusals, one cell each).
#   4. b5: the gate-home's <s8>/sweep.verdict is `red` -> b5's named refusal;
#      the file ABSENT -> the same refusal.
#   5. b6: the full sha is not an ancestor of refs/remotes/origin/<prefix>/
#      main -> the named refusal.
#   6. b7: the bare remote has NO refs/heads/master -> `ls-remote could not
#      read master`. (F9: run.log carries that line on the arm.)
#   7. b8: the remote's master already IS the full sha -> `master is <s8>
#      already` (rc 1, the push NEVER ran: the pre-receive refuse-log empty).
#   8. b9: the remote's master MOVED to a CHILD of the full sha -> the
#      fast-forward refusal naming the master's 8-hex, and the credential-
#      wired fetch fallback's FORCED refspec landed in the push repo's
#      refs/remotes/origin/master (the cell asserts it moved to the child).
#   9. b11/b12 the rejection class: the pre-receive HOOK refuses the push
#      (its allowlist does not name the pushed sha) -> b12's named refusal
#      `after the push, ls-remote reads master <m12>, not <s8>`; the remote's
#      master is UNCHANGED. (F8: run.log carries the redacted LAST line of
#      the refused push -- the hook's canary line, already redacted.)
#   10. b13 both redactor arms: the pre-receive's captured output carries a
#      40+-char CANARY token and a `://user:pass@` URL; both arrive in
#      run.log as `<r>` (the canary and the credential are ABSENT, `://<r>@`
#      present). (F6: the hook's clean short line arrives BYTE-UNCHANGED --
#      the 'clean output byte-unchanged' arm the review found unguarded.)
#   11. b14/b15 the success path: everything green -> rc 0, the summary
#      line's three readings (the re-read master, push.git origin/master,
#      the main checkout origin/master) all equal the full sha, the main
#      checkout's origin/master was refreshed, the pre-receive refuse-log
#      empty, and run.log carries the success line redacted-clean.
#      (b10-wording: stdout carries the EXACT `land: ls-remote master <m12>;
#      <s8> is a fast-forward (<N> commits)` line, N the expected count.)
#   12. b16 census: every refusal rc 1, the usage rc 2, the success rc 0
#      (one census over the recorded rcs of cells 1-11 + the wave-2a
#      refusal cells 13-18).
#   13. (F1, RED first) b2 refuses naming the RAW argument as passed: a
#      40-hex UNRESOLVING sha -> rc 1, `land: refused: <40hex> is not in
#      push.git` (the full 40 chars, NOT its first 8 -- land.sh:21 dies with
#      the raw argument; the s8 re-derive at :22 is after the die).
#   14. (F2, RED first) b5 strips ONLY trailing newlines, like the shell's
#      `$(cat)`: a sweep.verdict of `green` + a trailing space -> rc 1,
#      `land: refused: no green sweep verdict for <s8>` (today the port's
#      trims drop the space and ACCEPT).
#   15. (F3a, RED first) b3 is `tail -1 | grep ' rc=0$'`: a meta ending in a
#      BLANK line (`make ci rc=0\n\n`) -> the last line is the EMPTY line ->
#      rc 1, `no make ci record with rc=0 for <s8>` (today the port strips
#      ALL trailing newlines and ACCEPTS).
#   16. (F3b, RED first) b4 is `grep -q '^ci: OK'`, which matches a final
#      line WITHOUT a trailing newline: meta `make ci rc=0\n` + log
#      `ci: OK (x)` with NO trailing newline -> the verb must get PAST b4;
#      with no sweep.verdict the NEXT refusal is b5's `no green sweep
#      verdict` (today b4 itself refuses the newline-less file).
#   17. (F10) the b15 refusal arm (teethed): the standard success fixture,
#      but the main checkout's origin is a DIFFERENT bare repo whose master
#      sits at the OLD sha -> rc 1; STDOUT (not stderr) carries exactly
#      `land: the main checkout's origin/master was not refreshed`; the
#      three-reading summary line BEFORE it shows the main-checkout reading
#      = the old sha's 8 chars; the remote's master IS full (the push itself
#      succeeded).
#   18. (A5) b12's empty-M2 branch (teethed): a post-receive hook ACCEPTS
#      the push then DELETES refs/heads/master -> rc 1, and the refusal
#      `land: refused: after the push, ls-remote reads master , not <s8>` --
#      with the EMPTY m12 field (TWO spaces where the 12-hex reading would
#      be, the shell's `printf '%.12s' ''` bytes); the exact byte shape is
#      asserted.
#
# THE FIXTURE. Under $tmp (real git, a local BARE remote + two clones): a
# seed repo with the fixed chain C0<-C1<-C2<-C3<-C4 (C2 a side commit off
# C1, so C2 is on master's side but NOT under the team tip C3); a bare
# remote.git whose pre-receive hook rejects any push whose sha is not in an
# allowlist file (empty at base) and, on a reject, prints a clean short line
# plus a line carrying a 40+-char CANARY token and a `://user:pass@` URL
# (the b13 arms); push.git (a clone of the remote, the harness's own push
# repo) and main.git (a second clone, land.sh's $J main checkout); a
# fabricated ci-<s8>.log + ci-<s8>.log.meta (the orchestrator's byte shapes:
# the .meta's LAST line ` rc=0`/` rc=1`, the .log carrying a `ci: OK` line
# or not); a gate-home dir with <s8>/sweep.verdict (green/red/absent); a
# canary TOKEN FILE the config's gitTokenFile points at (never a real key),
# and a config whose drive.branchPrefix is the team branch. The driver runs
# the tree's binary by path ($HBIN, the third_program idiom) and -- for
# smoke_lint check 5 -- also runs the tree's "$BIN" once. Every fixture
# commit carries a FIXED date and -c user.name/-c user.email, and git -C
# everywhere (no cd), so the chain's shas -- and therefore the cells'
# expectations -- do not depend on the operator's clock.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"

t_plan 19
smoke_home
tmp=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# Build the harness binary once, like the sweep's neighbours (a hollow
# binary makes every cell not ok, per the sweep's hollowing rule).
_mk=$(smoke_make)
(cd "$SMOKE_ROOT" && $_mk jichi-harness -s) >/dev/null 2>&1

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

PFX="drive/x-land"
LP_USER="Land Smoke"
LP_EMAIL="land-smoke@example.com"
# The canary: a 40+-char token that is never a real key. The port's
# credential helper is pointed at a file holding it; the redactor (b13) must
# strip it from every captured git output line.
LP_CANARY="canarygittoken0123456789abcdefghij00000000000000"

# A fixture failure emits one t_fail per cell so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt 18 ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

# --- the base fixture: seed chain + bare remote (pre-receive) + 2 clones ----
# Rebuilt fresh per cell (a clean remote-tracking state and an empty
# pre-receive allowlist); the per-cell overrides then mutate the remote's
# refs and the ci / verdict / allowlist files.
lp_seed_commit() { # $1=msg $2=date; echoes the new sha
    _lp_f="$tmp/seed/f"
    echo "$1" >> "$_lp_f"
    git -C "$tmp/seed" add f 2>/dev/null
    GIT_AUTHOR_DATE="$2" GIT_COMMITTER_DATE="$2" \
        git -C "$tmp/seed" -c user.name="$LP_USER" -c user.email="$LP_EMAIL" \
        commit -q -m "$1"
    git -C "$tmp/seed" rev-parse HEAD
}
lp_base() {
    rm -rf "$tmp/seed" "$tmp/remote.git" "$tmp/other.git" "$tmp/push" \
           "$tmp/main" "$tmp/ci" "$tmp/gh" "$tmp/dir"
    mkdir -p "$tmp/ci" "$tmp/gh" "$tmp/dir"
    git init -q -b master "$tmp/seed" || return 1
    git -C "$tmp/seed" config user.email "$LP_EMAIL"
    git -C "$tmp/seed" config user.name "$LP_USER"
    echo seed > "$tmp/seed/f"; git -C "$tmp/seed" add f
    GIT_AUTHOR_DATE="2026-11-09T00:00:00Z" GIT_COMMITTER_DATE="2026-11-09T00:00:00Z" \
        git -C "$tmp/seed" commit -q -m C0
    LP_C0=$(git -C "$tmp/seed" rev-parse HEAD)
    LP_C1=$(lp_seed_commit C1 "2026-11-09T00:00:01Z")
    # C2: a side commit off C1 (on master's side, NOT under the team tip C3).
    git -C "$tmp/seed" checkout -q -b side "$LP_C1"
    LP_C2=$(lp_seed_commit C2 "2026-11-09T00:00:02Z")
    git -C "$tmp/seed" checkout -q master
    # C3 (the team tip / the success full) and C4 (a child of C3, the
    # pre-push base master, so the success push C3:master is a real
    # fast-forward).
    LP_C3=$(lp_seed_commit C3 "2026-11-09T00:00:03Z")
    LP_C4=$(lp_seed_commit C4 "2026-11-09T00:00:04Z")
    # C5: cell 8's moved master (a child of C4, NOT an ancestor of the team
    # tip, so b9's ff test refuses). It is seeded on a NON-HEADS ref (the
    # clone's `+refs/heads/*` fetch spec never sees it), so C5's object is
    # NOT in the push repo: b9's cat-file -e must FAIL, and the
    # CREDENTIAL-WIRED FETCH FALLBACK (land.sh:31) is the only path that can
    # move the push repo's origin/master -- the cell's decisive observable.
    LP_C5=$(lp_seed_commit C5 "2026-11-09T00:00:05Z")
    git -C "$tmp/seed" reset -q --hard "$LP_C4"
    git -C "$tmp/seed" checkout -q -b "$PFX/main" "$LP_C3"
    # The bare remote. Seed its objects FIRST (a hookless push from the seed
    # repo, master + the team branch + C4 on a throwaway ref), so the cells'
    # `git update-ref` of the remote's refs need no further push (and no
    # pre-receive) -- only the port's land push is gated by the hook.
    git init -q --bare "$tmp/remote.git" || return 1
    git -C "$tmp/seed" push -q "$tmp/remote.git" \
        "refs/heads/master:refs/heads/master" \
        "refs/heads/$PFX/main:refs/heads/$PFX/main" \
        "$LP_C2:refs/heads/tmp_c2" \
        "$LP_C4:refs/heads/tmp_c4" \
        "$LP_C5:refs/move/c5" || return 1
    git -C "$tmp/remote.git" update-ref -d refs/heads/tmp_c2
    git -C "$tmp/remote.git" update-ref -d refs/heads/tmp_c4
    # Reset master to C1 BEFORE the clones below (the seed's master tip is C4;
    # the clones must track C1 so C3 is a fast-forward of the base master, and
    # so C4/C2 are NOT in the push repo's objects -- cell 8's b9 fetch
    # fallback is the path that must fetch C4 in).
    git -C "$tmp/remote.git" update-ref refs/heads/master "$LP_C1"
    # The pre-receive hook (the rejection cell's stand-in for a protected
    # master): allow only a sha named in $tmp/allow. On EVERY push to master
    # it prints a short CLEAN line (the F6 arm: it must arrive in run.log
    # byte-unchanged through the redactor); on a reject it ALSO prints a
    # line carrying the canary token and a ://user:pass@ URL (the b13 arms)
    # and exits NON-ZERO (the pre-receive convention: 1 = refuse the push).
    cat > "$tmp/remote.git/hooks/pre-receive" <<EOF
#!/bin/sh
# stand-in for the remote's protected-master pre-receive: allow only the sha
# named in $tmp/allow.
while read old new ref; do
    case "\$ref" in
        refs/heads/master)
            echo "hook: pre-receive (checking)"
            if [ ! -f "$tmp/allow" ] || ! grep -qF "\$new" "$tmp/allow"; then
                echo "pre-receive: refusing master push \$new (token=$LP_CANARY remote=http://user:$LP_CANARY@remote.example/repo.git)" >&2
                echo "REFUSED \$new" >> "$tmp/refused"
                exit 1
            fi
            ;;
    esac
done
exit 0
EOF
    chmod +x "$tmp/remote.git/hooks/pre-receive"
    : > "$tmp/allow"
    : > "$tmp/refused"
    # The two clones. push.git is the harness's own push repo (origin=remote);
    # main.git is the main checkout (land.sh's $J, origin=remote).
    # file:// (NOT a plain path): a plain-path clone HARDLINKS the source's
    # objects, so C5 (seeded on refs/move/c5, unreachable from any heads/*)
    # would ALREADY be in push.git's objects and b9's cat-file -e would
    # SUCCEED, skipping the credential-wired fetch fallback. file:// fetches
    # only the reachable refs, so C5 stays absent and the fallback (land.sh
    # :31) is the path that moves origin/master -- the cell's observable.
    git clone -q "file://$tmp/remote.git" "$tmp/push" || return 1
    git clone -q "file://$tmp/remote.git" "$tmp/main" || return 1
    # The base remote: master at C1 (so C3 is a fast-forward of it), the
    # team branch at C3; each cell sets what it needs (lp_master / lp_team).
    # The canary token file (the config's gitTokenFile reference) + the config.
    printf '%s\n' "$LP_CANARY" > "$tmp/dir/token"; chmod 600 "$tmp/dir/token"
    printf 'k\n' > "$tmp/dir/key"; chmod 600 "$tmp/dir/key"
    printf '{"dir":"%s/dir","modelKeyFile":"%s/dir/key","gitTokenFile":"%s/dir/token","drive":{"branchPrefix":"%s"}}\n' \
        "$tmp" "$tmp" "$tmp" "$PFX" > "$tmp/cfg.json"
    # The success full sha (C3) and its s8.
    LP_FULL="$LP_C3"
    LP_S8=$(printf '%.8s' "$LP_FULL")
    # The base ci record (rc=0 + a 'ci: OK' line) and the green verdict.
    lp_ci 0 "OK"
    lp_verdict green
}

# lp_ci RC OK|NOOK -- fabricate the ci-<s8>.log.meta + the ci-<s8>.log
# (the orchestrator's byte shapes, b3/b4): the .meta's LAST line ends in
# ` rc=<RC>` (a leading space before rc, land.sh:23's ` rc=0$` grep); the
# .log carries a `ci: OK` line when OK, else only a non-OK first column.
lp_ci() {
    printf 'make ci rc=%s\n' "$1" > "$tmp/ci/ci-$LP_S8.log.meta"
    if [ "$2" = OK ]; then
        printf 'ci: OK\n' > "$tmp/ci/ci-$LP_S8.log"
    else
        printf 'ci: building\n' > "$tmp/ci/ci-$LP_S8.log"
    fi
}

# lp_verdict green|red|absent -- the gate-home's <s8>/sweep.verdict.
lp_verdict() {
    if [ "$1" = absent ]; then
        rm -f "$tmp/gh/$LP_S8/sweep.verdict"
    else
        mkdir -p "$tmp/gh/$LP_S8"
        printf '%s\n' "$1" > "$tmp/gh/$LP_S8/sweep.verdict"
    fi
}

# lp_allow SHA -- add a sha to the pre-receive allowlist (empty = reject all).
lp_allow() { [ -n "$1" ] && echo "$1" >> "$tmp/allow"; : > "$tmp/refused"; }

# lp_master SHA -- set the remote's refs/heads/master to SHA ('' deletes it).
lp_master() {
    if [ -z "$1" ]; then
        git -C "$tmp/remote.git" update-ref -d refs/heads/master
    else
        git -C "$tmp/remote.git" update-ref refs/heads/master "$1"
    fi
}

# lp_team SHA -- set the remote's team branch (the push repo's tracking ref
# must be refreshed by a fetch first, or the b6 ancestry reads a stale ref).
lp_team() {
    git -C "$tmp/remote.git" update-ref "refs/heads/$PFX/main" "$1"
    git -C "$tmp/push" fetch -q origin "$PFX/main" 2>/dev/null
    git -C "$tmp/push" update-ref "refs/remotes/origin/$PFX/main" "$1" 2>/dev/null
}

# lp_run LOGFILE OUT ERR -- one `land` invocation (all five path flags +
# the config), returning rc through the caller's $?.
lp_run() {
    "$HBIN" --config "$tmp/cfg.json" land \
        --push-repo "$tmp/push" --ci-log-dir "$tmp/ci" \
        --gate-home "$tmp/gh" --main-checkout "$tmp/main" \
        --land-log "$tmp/$1" "$2" < /dev/null > "$tmp/$3" 2> "$tmp/$4"
}

# lp_log_empty FILE -- 1 when FILE is absent OR empty (the refusal cell's
# "run.log carries no reference output" assertion).
lp_log_empty() {
    [ -f "$1" ] || return 0
    [ ! -s "$1" ]
}

# lp_grep NEEDLE FILE -- 1 when FILE contains NEEDLE (fixed string).
lp_grep() { "$G" -qF -- "$1" "$2" 2>/dev/null; }

# The push's captured-output lines (git relays the remote's pre-receive
# output prefixed `remote: `, and a refused push ends with git's own error
# line; $tmp is fixed for the driver's run, so the bytes are known).
LP_CANARY_LINE="remote: pre-receive: refusing master push <r> (token=<r> remote=http://<r>@remote.example/repo.git)"
LP_CLEAN_LINE="remote: hook: pre-receive (checking)"
LP_PUSH_ERR_LINE="error: failed to push some refs to 'file://$tmp/remote.git'"

if ! lp_base; then
    fixture_fail "the base git fixture could not be built"
fi

# --- 1 (b1): usage -- no <SHA8> -> rc 2, names `usage: jichi-harness land <SHA8>`
"$HBIN" --config "$tmp/cfg.json" land \
    --push-repo "$tmp/push" --ci-log-dir "$tmp/ci" \
    --gate-home "$tmp/gh" --main-checkout "$tmp/main" \
    --land-log "$tmp/ll1" < /dev/null > "$tmp/o1" 2> "$tmp/e1"
rc1=$?
if [ "$rc1" -eq 2 ] && lp_grep 'usage: jichi-harness land <SHA8>' "$tmp/e1"; then
    t_ok "1: b1 usage -- no <SHA8> -> rc 2, stderr names usage: jichi-harness land <SHA8>"
else
    t_fail "1: b1 usage expected rc 2 + 'usage: jichi-harness land <SHA8>' (got rc=$rc1: $(tr '\n' '|' < "$tmp/e1" | head_bytes 200))"
fi

# --- 2 (b2): the sha is not in push.git -> named refusal, rc 1, NO ci read
BAD8="deadbeef"
lp_run ll2 "$BAD8" o2 e2; rc2=$?
if [ "$rc2" -eq 1 ] && lp_grep "land: refused: $BAD8 is not in push.git" "$tmp/e2" \
   && lp_log_empty "$tmp/ll2"; then
    t_ok "2: b2 -- sha $BAD8 not in push.git -> 'land: refused: $BAD8 is not in push.git' (rc 1), run.log empty (no ci read)"
else
    t_fail "2: b2 expected rc 1 + the named refusal + an empty run.log (got rc=$rc2: $(tr '\n' '|' < "$tmp/e2" | head_bytes 200); run.log: $(head_bytes 120 "$tmp/ll2" 2>/dev/null))"
fi

# --- 3 (b3/b4): the ci record's last line ` rc=1` -> b3; a ` rc=0` record
#      whose log has no `^ci: OK` line -> b4 (two refusals, one cell each)
lp_ci 1 OK
lp_run ll3 "$LP_S8" o3a e3a; rc3a=$?
lp_ci 0 NOOK
lp_run ll3 "$LP_S8" o3b e3b; rc3b=$?
if [ "$rc3a" -eq 1 ] && lp_grep "land: refused: no make ci record with rc=0 for $LP_S8" "$tmp/e3a" \
   && [ "$rc3b" -eq 1 ] && lp_grep "no 'ci: OK' line in ci-$LP_S8.log" "$tmp/e3b"; then
    t_ok "3: b3 (meta last line ' rc=1') -> 'no make ci record with rc=0 for <s8>'; b4 (rc=0 but no '^ci: OK' line) -> \"no 'ci: OK' line in ci-<s8>.log\" (both rc 1)"
else
    t_fail "3: b3/b4 expected the two named refusals (got rc=$rc3a/$rc3b: $(tr '\n' '|' < "$tmp/e3a" | head_bytes 160) || $(tr '\n' '|' < "$tmp/e3b" | head_bytes 160))"
fi
lp_ci 0 OK   # restore the base ci record

# --- 4 (b5): sweep.verdict `red` -> named refusal; the file ABSENT -> same
lp_verdict red; lp_run ll4 "$LP_S8" o4a e4a; rc4a=$?
lp_verdict absent; lp_run ll4 "$LP_S8" o4b e4b; rc4b=$?
lp_verdict green
if [ "$rc4a" -eq 1 ] && lp_grep "land: refused: no green sweep verdict for $LP_S8" "$tmp/e4a" \
   && [ "$rc4b" -eq 1 ] && lp_grep "land: refused: no green sweep verdict for $LP_S8" "$tmp/e4b"; then
    t_ok "4: b5 -- sweep.verdict 'red' AND absent both -> 'land: refused: no green sweep verdict for <s8>' (rc 1)"
else
    t_fail "4: b5 expected the named refusal for red and absent (got rc=$rc4a/$rc4b: $(tr '\n' '|' < "$tmp/e4a" | head_bytes 160) || $(tr '\n' '|' < "$tmp/e4b" | head_bytes 160))"
fi

# --- 5 (b6): the full sha is not on the remote's team branch
lp_team "$LP_C2"   # the team tip no longer contains C3 (the side commit C2 is not an ancestor of C3)
lp_run ll5 "$LP_S8" o5 e5; rc5=$?
lp_team "$LP_C3"   # restore
if [ "$rc5" -eq 1 ] && lp_grep "land: refused: $LP_S8 is not on the remote's team branch" "$tmp/e5"; then
    t_ok "5: b6 -- $LP_S8 not an ancestor of the team tip -> 'land: refused: <s8> is not on the remote's team branch' (rc 1)"
else
    t_fail "5: b6 expected the named refusal (got rc=$rc5: $(tr '\n' '|' < "$tmp/e5" | head_bytes 200))"
fi

# --- 6 (b7): the bare remote has NO refs/heads/master
#     (F9: run.log carries the refusal line on the arm)
lp_master ""
lp_run ll6 "$LP_S8" o6 e6; rc6=$?
lp_master "$LP_C1"   # restore
if [ "$rc6" -eq 1 ] && lp_grep "land: refused: ls-remote could not read master" "$tmp/e6" \
   && lp_grep 'ls-remote could not read master' "$tmp/ll6"; then
    t_ok "6: b7 -- the remote has no refs/heads/master -> 'land: refused: ls-remote could not read master' (rc 1), run.log carries the line"
else
    t_fail "6: b7 expected the named refusal + run.log carrying it (got rc=$rc6: $(tr '\n' '|' < "$tmp/e6" | head_bytes 200); run.log: $(tr '\n' '|' < "$tmp/ll6" 2>/dev/null | head_bytes 160))"
fi

# --- 7 (b8): master is already the full sha -> idempotency guard
lp_master "$LP_C3"
lp_run ll7 "$LP_S8" o7 e7; rc7=$?
lp_master "$LP_C1"   # restore
if [ "$rc7" -eq 1 ] && lp_grep "land: refused: master is $LP_S8 already" "$tmp/e7" \
   && [ ! -s "$tmp/refused" ]; then
    t_ok "7: b8 -- the remote's master already IS $LP_S8 -> 'land: refused: master is <s8> already' (rc 1), the push never ran (pre-receive refuse-log empty)"
else
    t_fail "7: b8 expected the named refusal + an empty refuse-log (got rc=$rc7: $(tr '\n' '|' < "$tmp/e7" | head_bytes 200); refuse-log: $(head_bytes 120 "$tmp/refused"))"
fi

# --- 8 (b9): master MOVED to a child of the full sha (C5, an object that is
#      NOT in the push repo: the clone never saw refs/move/c5) -> cat-file -e
#      fails, the fetch fallback must run, and the ff refusal names C5's 8.
C5_8=$(printf '%.8s' "$LP_C5")
lp_master "$LP_C5"
lp_run ll8 "$LP_S8" o8 e8; rc8=$?
P8=$(git -C "$tmp/push" rev-parse --verify -q refs/remotes/origin/master 2>/dev/null)
lp_master "$LP_C1"   # restore
if [ "$rc8" -eq 1 ] && lp_grep "land: refused: $LP_S8 is not a fast-forward of master $C5_8 (ls-remote)" "$tmp/e8" \
   && [ "$P8" = "$LP_C5" ]; then
    t_ok "8: b9 -- master moved to a child ($C5_8) of $LP_S8 -> 'land: refused: <s8> is not a fast-forward of master $C5_8 (ls-remote)' (rc 1), and the credential-wired fetch fallback's forced refspec moved push.git origin/master to $LP_C5"
else
    t_fail "8: b9 expected the ff refusal naming $C5_8 + origin/master moved to the child (got rc=$rc8: $(tr '\n' '|' < "$tmp/e8" | head_bytes 200); push origin/master=$P8)"
fi

# --- 9 (b11/b12): the pre-receive REFUSES the push -> the re-read refusal
#     (F8: run.log carries the redacted LAST line of the refused push)
lp_allow ""            # empty allowlist: the push is rejected
lp_run ll9 "$LP_S8" o9 e9; rc9=$?
M9=$(git -C "$tmp/remote.git" rev-parse --verify -q refs/heads/master 2>/dev/null)
if [ "$rc9" -eq 1 ] && lp_grep "land: refused: after the push, ls-remote reads master $(printf '%.12s' "$LP_C1"), not $LP_S8" "$tmp/e9" \
   && [ "$M9" = "$LP_C1" ] \
   && lp_grep "$LP_PUSH_ERR_LINE" "$tmp/ll9" \
   && lp_grep "$LP_CANARY_LINE" "$tmp/ll9"; then
    t_ok "9: b11/b12 -- the pre-receive refuses the push -> 'land: refused: after the push, ls-remote reads master <m12>, not <s8>' (rc 1), the remote's master is unchanged ($LP_C1), run.log carries the redacted LAST line of the refused push + the hook's redacted canary line"
else
    t_fail "9: b11/b12 expected the re-read refusal + an unchanged master + run.log's redacted push tail (got rc=$rc9: $(tr '\n' '|' < "$tmp/e9" | head_bytes 220); master=$M9; run.log: $(tr '\n' '|' < "$tmp/ll9" 2>/dev/null | head_bytes 300))"
fi

# --- 10 (b13): both redactor arms on cell 9's captured push output
#     the pre-receive printed the canary + a ://user:pass@ URL; both must
#     arrive in run.log as <r> (the canary and the credential ABSENT).
#     (F6: the hook's CLEAN short line arrives BYTE-UNCHANGED.)
if lp_grep '://<r>@' "$tmp/ll9" && lp_grep '<r>' "$tmp/ll9" \
   && ! lp_grep "$LP_CANARY" "$tmp/ll9" \
   && lp_grep "$LP_CLEAN_LINE" "$tmp/ll9"; then
    t_ok "10: b13 -- the credential-wired push's captured output is redacted: a ://user:pass@ URL -> : //<r>@ and the 40+-char canary -> <r> (the canary absent from run.log), and the hook's clean short line arrives byte-unchanged"
else
    t_fail "10: b13 expected the redactor's both arms + the byte-unchanged clean line in run.log (run.log: $(tr '\n' '|' < "$tmp/ll9" 2>/dev/null | head_bytes 300))"
fi

# --- 11 (b14/b15): the success path -- everything green
#     (b10-wording: stdout carries the EXACT success line, N the count)
lp_allow "$LP_C3"      # the pre-receive accepts the push of C3
lp_run ll11 "$LP_S8" o11 e11; rc11=$?
M11=$(git -C "$tmp/remote.git" rev-parse --verify -q refs/heads/master 2>/dev/null)
P11=$(git -C "$tmp/push" rev-parse --verify -q refs/remotes/origin/master 2>/dev/null)
J11=$(git -C "$tmp/main" rev-parse --verify -q refs/remotes/origin/master 2>/dev/null)
if [ "$rc11" -eq 0 ] && [ "$M11" = "$LP_C3" ] && [ "$P11" = "$LP_C3" ] && [ "$J11" = "$LP_C3" ] \
   && lp_grep "land: ls-remote master $(printf '%.12s' "$LP_C1"); $LP_S8 is a fast-forward (1 commits)" "$tmp/o11" \
   && lp_grep "land: master is $(printf '%.12s' "$LP_C3") (ls-remote); push.git origin/master $LP_S8; main checkout origin/master $LP_S8" "$tmp/o11" \
   && [ ! -s "$tmp/refused" ] && ! lp_grep "$LP_CANARY" "$tmp/ll11"; then
    t_ok "11: b14/b15 -- the success path -> rc 0, the EXACT b10 line 'land: ls-remote master <m12>; <s8> is a fast-forward (1 commits)' + the summary line's three readings all equal the full sha, the main checkout was refreshed, the refuse-log is empty, run.log redacted-clean"
else
    t_fail "11: b14/b15 expected rc 0 + the exact b10 line + the summary line + three equal readings (got rc=$rc11; remote=$M11 push=$P11 main=$J11; stdout: $(tr '\n' '|' < "$tmp/o11" | head_bytes 300))"
fi

# Restore the base state cell 11's success path mutated (the remote's master
# moved to C3, the allowlist carries C3) so cells 13-18 start from the base:
# master at C1, an EMPTY allowlist (lp_allow is append-only: clear it by
# truncation, not by a '' call).
lp_master "$LP_C1"
: > "$tmp/allow"
: > "$tmp/refused"

# --- 13 (F1): the b2 refusal names the RAW argument as passed (land.sh:21
#      dies BEFORE the :22 s8 re-derive). A 40-hex UNRESOLVING sha must be
#      named in FULL (40 chars), not its first 8.
BAD40=$(printf 'e%.0s' $(seq 40))
lp_run ll13 "$BAD40" o13 e13; rc13=$?
if [ "$rc13" -eq 1 ] && lp_grep "land: refused: $BAD40 is not in push.git" "$tmp/e13" \
   && ! lp_grep "land: refused: $(printf '%.8s' "$BAD40") is not in push.git" "$tmp/e13" \
   && lp_log_empty "$tmp/ll13"; then
    t_ok "13: F1 -- a 40-hex unresolving argument -> 'land: refused: <40hex> is not in push.git' naming the RAW argument (rc 1), not its first 8 chars"
else
    t_fail "13: F1 expected rc 1 + the refusal naming the RAW 40-char argument (got rc=$rc13: $(tr '\n' '|' < "$tmp/e13" | head_bytes 200); run.log: $(head_bytes 120 "$tmp/ll13" 2>/dev/null))"
fi

# --- 14 (F2): the sweep verdict is `green` + a trailing space. The shell's
#      $(cat) strips trailing NEWLINES only, so the port must REFUSE (today
#      it trims the space and accepts).
printf 'green \n' > "$tmp/gh/$LP_S8/sweep.verdict"
lp_run ll14 "$LP_S8" o14 e14; rc14=$?
lp_verdict green   # restore
# (a green-pre-fix run pushed C3: restore the base master + empty allowlist)
lp_master "$LP_C1"
: > "$tmp/allow"
: > "$tmp/refused"
if [ "$rc14" -eq 1 ] && lp_grep "land: refused: no green sweep verdict for $LP_S8" "$tmp/e14"; then
    t_ok "14: F2 -- sweep.verdict 'green ' (trailing space) -> 'land: refused: no green sweep verdict for <s8>' (rc 1): only trailing NEWLINES are stripped, like \$(cat)"
else
    t_fail "14: F2 expected rc 1 + the named refusal for 'green ' (got rc=$rc14: $(tr '\n' '|' < "$tmp/e14" | head_bytes 200))"
fi

# --- 15 (F3a): the ci meta ends in a BLANK line ('make ci rc=0\n\n').
#      tail -1 of such a file is the EMPTY last line, which does not match
#      ' rc=0$' -> the port must REFUSE (today it strips all trailing
#      newlines and accepts the blank's predecessor).
printf 'make ci rc=0\n\n' > "$tmp/ci/ci-$LP_S8.log.meta"
lp_run ll15 "$LP_S8" o15 e15; rc15=$?
lp_ci 0 OK   # restore
if [ "$rc15" -eq 1 ] && lp_grep "land: refused: no make ci record with rc=0 for $LP_S8" "$tmp/e15"; then
    t_ok "15: F3a -- ci meta ending in a blank line ('make ci rc=0\\n\\n') -> 'land: refused: no make ci record with rc=0 for <s8>' (rc 1): tail -1 is the empty last line"
else
    t_fail "15: F3a expected rc 1 + the named refusal for the blank-line meta (got rc=$rc15: $(tr '\n' '|' < "$tmp/e15" | head_bytes 200))"
fi
lp_master "$LP_C1"   # restore (a green-pre-fix run pushes C3)
: > "$tmp/allow"
: > "$tmp/refused"

# --- 16 (F3b): the ci log's final 'ci: OK' line has NO trailing newline.
#      grep -q '^ci: OK' matches it, so b4 must PASS; with the sweep
#      verdict absent the NEXT refusal is b5's (today b4 itself refuses).
printf 'ci: OK (x)' > "$tmp/ci/ci-$LP_S8.log"
lp_verdict absent
lp_run ll16 "$LP_S8" o16 e16; rc16=$?
lp_ci 0 OK
lp_verdict green   # restore
lp_master "$LP_C1"   # restore (a green-pre-fix run pushes C3)
: > "$tmp/allow"
: > "$tmp/refused"
if [ "$rc16" -eq 1 ] && lp_grep "land: refused: no green sweep verdict for $LP_S8" "$tmp/e16" \
   && ! lp_grep "ci: OK" "$tmp/e16"; then
    t_ok "16: F3b -- ci log ending 'ci: OK (x)' with NO trailing newline passes b4 like grep does: the next refusal is b5's (the verdict is absent), not b4's"
else
    t_fail "16: F3b expected b4 to pass + b5's refusal next (got rc=$rc16: $(tr '\n' '|' < "$tmp/e16" | head_bytes 200))"
fi

# --- 17 (F10): the b15 refusal arm -- the main checkout's origin is a
#      DIFFERENT bare repo whose master sits at the OLD sha (C1): the push
#      itself lands the remote at C3, but the main checkout's refresh can
#      never read C3. Expect rc 1, the plain STDOUT line, the summary
#      BEFORE it showing the main reading = C1's 8, and the remote FULL.
lp_allow "$LP_C3"
git init -q --bare "$tmp/other.git" || t_fail "17: F10 could not build the second bare remote"
git -C "$tmp/seed" push -q "$tmp/other.git" "refs/heads/master:refs/heads/master" \
    || t_fail "17: F10 could not seed the second bare remote"
git -C "$tmp/other.git" update-ref refs/heads/master "$LP_C1"
git -C "$tmp/main" remote set-url origin "file://$tmp/other.git"
lp_run ll17 "$LP_S8" o17 e17; rc17=$?
# restore: the main checkout's origin back to the base remote (the run's
# push of C3 to the base remote is harmless: master is restored below).
git -C "$tmp/main" remote set-url origin "file://$tmp/remote.git"
M17=$(git -C "$tmp/remote.git" rev-parse --verify -q refs/heads/master 2>/dev/null)
C1_8=$(printf '%.8s' "$LP_C1")
if [ "$rc17" -eq 1 ] \
   && lp_grep "land: the main checkout's origin/master was not refreshed" "$tmp/o17" \
   && ! lp_grep "land: the main checkout's origin/master was not refreshed" "$tmp/e17" \
   && lp_grep "land: master is $(printf '%.12s' "$LP_C3") (ls-remote); push.git origin/master $LP_S8; main checkout origin/master $C1_8" "$tmp/o17" \
   && [ "$M17" = "$LP_C3" ]; then
    t_ok "17: F10 -- b15 arm: the main checkout's origin points at a bare whose master is the OLD sha -> rc 1, STDOUT carries the plain line, the summary before it shows the main reading = $C1_8, and the remote's master IS full ($LP_C3)"
else
    t_fail "17: F10 expected rc 1 + the stdout plain line + the summary with the old main reading + a full remote (got rc=$rc17; remote=$M17; stdout: $(tr '\n' '|' < "$tmp/o17" | head_bytes 300); stderr: $(tr '\n' '|' < "$tmp/e17" | head_bytes 200))"
fi
# The run's push DID land C3 on the base remote (only the main checkout's
# refresh refused): restore the base master so cell 18 starts from C1.
lp_master "$LP_C1"

# --- 18 (A5): b12's empty-M2 branch -- a post-receive hook ACCEPTS the push
#      (the allowlist is satisfied) but then DELETES refs/heads/master: the
#      re-read's ls-remote has no first field -> the refusal with the EMPTY
#      m12 (TWO spaces where the 12-hex reading would be, the shell's
#      printf '%.12s' '' bytes). The exact byte shape is asserted.
lp_allow "$LP_C3"
cat > "$tmp/remote.git/hooks/post-receive" <<EOF
#!/bin/sh
# A5 stand-in: accept the push (pre-receive did), then DELETE the ref, so
# the port's re-read (ls-remote) finds NO master.
git update-ref -d refs/heads/master
exit 0
EOF
chmod +x "$tmp/remote.git/hooks/post-receive"
lp_run ll18 "$LP_S8" o18 e18; rc18=$?
M18=$(git -C "$tmp/remote.git" rev-parse --verify -q refs/heads/master 2>/dev/null)
rm -f "$tmp/remote.git/hooks/post-receive"
lp_master "$LP_C1"   # restore
if [ "$rc18" -eq 1 ] && lp_grep "land: refused: after the push, ls-remote reads master , not $LP_S8" "$tmp/e18" \
   && ! lp_grep "reads master $(printf '%.12s' "$LP_C1")" "$tmp/e18" \
   && [ -z "$M18" ]; then
    t_ok "18: A5 -- b12 empty-M2 arm: a post-receive deletes the ref after the push -> 'land: refused: after the push, ls-remote reads master , not <s8>' (rc 1, the EMPTY m12: two spaces where the 12-hex reading would be), the remote's master is gone"
else
    t_fail "18: A5 expected rc 1 + the refusal with the EMPTY m12 field (got rc=$rc18; master=$M18; stderr: $(tr '\n' '|' < "$tmp/e18" | head_bytes 300))"
fi
lp_master "$LP_C1"
: > "$tmp/allow"
: > "$tmp/refused"

# --- 12 (b16): the census -- every refusal rc 1, the usage rc 2, the success rc 0
#     (over the rcs of cells 1-11 plus the wave-2a refusal cells 13-18, so a
#     rc drift in ANY cell surfaces twice.)
want_rcs="$rc2 $rc3a $rc3b $rc4a $rc4b $rc5 $rc6 $rc7 $rc8 $rc9 $rc1 $rc11 $rc13 $rc14 $rc15 $rc16 $rc17 $rc18"
if [ "$want_rcs" = "1 1 1 1 1 1 1 1 1 1 2 0 1 1 1 1 1 1" ]; then
    t_ok "12: b16 -- every refusal rc 1 (cells 2-9 and 13-18, the sixteen refusal runs), the usage rc 2 (cell 1), the success rc 0 (cell 11): the exit contract holds"
else
    t_fail "12: b16 census mismatch (got: $want_rcs)"
fi

# --- 19: the <SHA8> must be a commit id, not a name that git resolves -----------
# land hands its argument to `git rev-parse --verify "<arg>^{commit}"`, and
# until this cell only an EMPTY argument was refused. So `land HEAD`, or a
# branch name, was taken: it landed whatever that name pointed at when land
# ran, which need not be the commit the gate had checked. `gate run` already
# asks for 7 to 40 lowercase hex digits; land asks the same now, before it
# touches git. HEAD resolves in the fixture's push repository, so on the old
# program this cell is not a usage refusal.
"$HBIN" --config "$tmp/cfg.json" land HEAD \
    --push-repo "$tmp/push" --ci-log-dir "$tmp/ci" \
    --gate-home "$tmp/gh" --main-checkout "$tmp/main" \
    --land-log "$tmp/ll19" < /dev/null > "$tmp/o19" 2> "$tmp/e19"
rc19=$?
if [ "$rc19" -eq 2 ] && lp_grep "the <SHA8> must be 7-40 lowercase hex (got 'HEAD')" "$tmp/e19" \
    && [ ! -s "$tmp/ll19" ]; then
    t_ok "19: a name in place of the <SHA8> (HEAD) is a usage refusal: rc 2, the hex rule named, no land log written"
else
    t_fail "19: \`land HEAD\` must be refused as usage (rc 2, 'the <SHA8> must be 7-40 lowercase hex (got 'HEAD')', an empty land log); got rc=$rc19, log bytes $(wc -c < "$tmp/ll19" 2>/dev/null | tr -d ' '), stderr: $(tr '\n' '|' < "$tmp/e19" | head_bytes 200)"
fi

t_done
