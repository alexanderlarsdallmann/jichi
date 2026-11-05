#!/bin/sh
# smoke: `jichi-harness mirror push` + `credential-helper` -- the C89 port of
# mirror.sh's mirror_push + pre-push-keyscan.sh + pre-push-sizecheck.sh +
# git-credential-from-file (the jichi-harness drive of 2026-10-23, batch H3c,
# M1056). The fixture is the M1055 driver's rehearsal layout (init ->
# standin.git / team / mirror.git + the stored config copy), and this driver
# FIRST runs `mirror fetch` (M1055, built) to populate each mirror, so `push`
# has a real remote (the stand-in) and a mirror with the team's refs.
#
# M1056 (H3c). Design note (the contract, measured on git 2.53.0):
#   docs/internal/drive/2026-10-23-harness/notes/M1056.md
# Ported reference (read line by line; this is the PUSH half only):
#   docs/internal/drive/2026-10-23-harness/harness/mirror.sh        (mirror_push)
#   docs/internal/drive/2026-10-23-harness/harness/pre-push-keyscan.sh
#   docs/internal/drive/2026-10-23-harness/harness/pre-push-sizecheck.sh
#   docs/internal/drive/2026-10-23-harness/harness/git-credential-from-file
#
# THE RED THIS IS HELD BY. On the tree before the change `mirror push` is a
# usage refusal (exit 2: "mirror: the verb must be 'fetch' or 'head'
# (got 'push')") and `credential-helper` is "unknown command" (exit 2).
# TWENTY of the twenty-two cells run the push or the helper and are therefore
# RED: they get exit 2 instead of 0/1 and the ref landings / report lines are
# absent. The two cells that pass on the red tree are controls that do NOT run
# the push under test: cell 12 (a plain `git push` proving the argv/env
# instrument runs) and cell 22 (the canary-leak sweep, which is satisfied
# trivially because nothing that could leak ever ran). A red-first driver is
# red on the FEATURE cells, not on its own controls.
#
# THE LAYOUT ORDERING (why there are FOUR rehearsal dirs). The port runs the
# KEY SCAN BEFORE the size check (the reference's pre-push-mirror.sh order),
# and each gate scans EVERY blob version the push adds (the reference's
# rev-list --objects range, excluding what the remote already tracks). So a
# canary cell contaminates its mirror's history for good: no later push on the
# same mirror can land, and its size verdict is masked by the key refusal.
# The driver therefore runs:
#   dir    -- the clean size/layout mirror (cells 1, 2, 3, 7, 8, 9, 10, 11, 16),
#   dir8   -- a FRESH clean mirror for the two happy pushes that must LAND
#             (cells 13, 15: the isolation env-dump and the argv capture),
#   dir2   -- a FRESH mirror whose team carries the canaries (cells 4, 5, 6:
#             the key-scan refusals, where the KEY verdict is the one to read),
#   dirE   -- a FRESH clean mirror with NO $BR tip, for the zero-tip
#             fail-closed cell (cell 12),
#   dir9   -- a FRESH clean mirror for the non-FF divergence (cell 26),
#   dir10  -- a FRESH rehearsal for the isolated token-canary key-scan
#             refusal (cell 6, P3c, added-then-deleted before the tip): its
#             ONLY secret is the token canary, so the token scan branch is
#             what refuses it,
#   dir11  -- a FRESH rehearsal for the two 06:37-review cells: cell 27
#             (P1ls, the ls-remote child's argv/env) and cell 28 (P7, the
#             pushKept gate, default off / on, a kept ref planted in the
#             mirror).
#
# WHAT IS CHECKED -- 31 cells, one t_ok/t_fail each (the note's driver-cells
# section, P1..P15; the multi-line cells are split into their own results).
# The 2026-10-25 06:37 review follow-up (H3c -f) adds two: cell 27 (P1's
# tooth: the ls-remote child's argv/env, the credential helper's full
# reset-then-set + the three identity env vars) and cell 28 (P7's tooth: the
# pushKept gate, default off -- a planted kept ref is NOT pushed -- and on --
# the same plant IS pushed to refs/heads/$BR/kept/planted).
# The 2026-10-25 review follow-up (F1/F2/F5, batch H3c -e) inserts four:
# P2c (cell 3, the URL guard's DISTINCT tooth, check 3 satisfied),
# P4e (cell 12, zero pushed tips fail-closed: a mirror lacking $BR/main is
# refused exit 1, never exit 0), P11g + P11h (cells 18-19, the credential
# identity's PATH half: a scheme URL with a wrong path is refused, a matching
# path is answered). The F1 reorder also changes cell 2 (P2): a mirror-local
# set-url/pushurl plant now changes the LIVE config bytes, so the git-free
# CHECK 3 refuses it BEFORE any git child (the guard's distinct tooth is
# cell 3, not cell 2). The non-FF push and the canary sweep keep their final
# positions (26, 29):
#   1.  P1    the happy push (dir): exit 0, the stand-in's $BR/main +
#             $BR/work/* equal the mirror's (the push LANDED), and the push
#             child's argv (a git wrapper on PATH) carries the stand-in's
#             ABSOLUTE path and NOT the bare `origin` name, with NO --force.
#   2.  P2    the MIRROR-LOCAL plants, caught by CHECK 3 (dir, F1): a
#             `remote set-url origin <a different bare repo>` / a `pushurl`
#             plant changes the LIVE config bytes (the stored copy untouched),
#             so the git-free CHECK 3 refuses it BEFORE any git child -- exit
#             2, the stand-in's refs UNCHANGED, and NO git child ran at all
#             (check 3 needs no git, so the wrapper dump is ABSENT). A
#             `pushurl` plant is a second line (same refusal). The guard's
#             DISTINCT tooth (a plant check 3 CANNOT see) is cell 3 (P2c).
#   3.  P2c   the URL guard's DISTINCT tooth, WITH check 3 satisfied (dir,
#             F1): AFTER init (live == stored), rewrite the `url =` line to
#             $tmp/other.git in BOTH the live config AND the stored copy (the
#             same substitution, so the config BYTES still match -- check 3
#             passes) plus a `pushurl` at the stand-in (so the EFFECTIVE
#             get-url --push stays the stand-in). The stored URL now != the
#             effective URL -> the guard alone refuses exit 2 naming both, the
#             stand-in UNCHANGED, a get-url child in the dump, no push child.
#   4.  P3    the key scan (dir2): a model-key-canary file committed in team
#             -> fetch -> `mirror push` refused exit 1 naming the file PATH
#             (not content), stand-in UNCHANGED.
#   5.  P3b   control (dir2): a canary EMBEDDED in surrounding text
#             (x<canary>y) is ALSO refused (substring match, not exact-token).
#   6.  P3c   perturbation (dir2): the token canary in a file ADDED in an
#             earlier commit and DELETED before the tip is still refused (the
#             range reaches it through history). Cell 27 is its ISOLATED form
#             (a fresh dir10 whose only secret is the token canary, so the
#             token scan branch -- not the key canary still in dir2's range --
#             is what refuses it, the 06:37-review finding).
#   7.  P3d   fail-closed (dir): the config's gitTokenFile pointed at a
#             MISSING file -> refused exit 1 naming the token, no push
#             attempted.
#   8.  P4    the size check (dir): a 9 MB blob in team -> refused exit 1
#             naming the file + size, stand-in UNCHANGED.
#   9.  P4b   control (dir8): a 1 MB blob (under the 8 MB per-file limit)
#             pushes clean: exit 0, landed.
#  10.  P4c   total-over (dir): two 12 MB files -> refused exit 1, the
#             per-push limit named.
#  11.  P4d   perturbation (dir): the big blobs already in the remote (landed
#             by the driver's DIRECT push -- its own instrument -- WITHOUT the
#             driver refreshing the mirror's tracking refs) are NOT re-counted:
#             the stand-in's tips sit PAST the stale tracking refs, so one new
#             1 MB blob pushes clean (exit 0) ONLY because the port excludes by
#             `git ls-remote <stored URL>` (a tracking-ref-only exclusion
#             re-counts the 9 MB blob and is refused).
#  12.  P4e   zero pushed tips is FAIL-CLOSED, not a pass (dirE, F5,
#             behaviour 14): a FRESH rehearsal whose mirror holds NO $BR/main
#             and NO $BR/work/* (init ONLY, no `mirror fetch`) -> `mirror
#             push` REFUSED exit 1 by the gate's OWN 'no pushed tip resolved
#             in the mirror; the push is refused (fail-closed)' line, the
#             stand-in UNCHANGED. A port that lets a zero-tip scan pass
#             (say() a '0 file version(s)' success line and return 0) is red
#             here: the prove-the-instrument moment.
#  13.  P8    the push's GLOBAL-config isolation, behavioural (dir8): a
#             SUBSHELL that unsets the driver's own GIT_CONFIG_* exports
#             and sets HOME=<fake home> whose .gitconfig plants a
#             credential.helper = an executable that touches a marker (and
#             answers a dummy credential). Assert: the push child's env dump
#             shows the harness's OWN empty GIT_CONFIG_GLOBAL file (exists, 0
#             bytes, under the harness's dir -- NOT merely `GIT_CONFIG_GLOBAL=
#             `; the driver's export is unset for this run, so only the
#             harness's value can be present), the marker is ABSENT (a
#             local-path push consults no helper anyway -- measured on git
#             2.53; the HTTPS-form fire is the drive's own loop, def: the
#             note's 'HTTPS end-to-end' deferred), and the push exits 0 with
#             the stand-in's refs == the mirror's. The cell's real value is
#             the env-dump assertion: the driver's export cannot satisfy it
#             once unset, so only the harness's own empty file can.
#  14.  P8ctl control (the INSTRUMENT runs): a plain `git -C <mirror> push`
#             from the same subshell env (no harness, no -c flags) -- for a
#             local-path push the marker is ALSO absent (local never
#             consults helpers), so the control proves the wrapper dumps, not
#             that the helper fires; cell 11's env-dump assertion is the tooth
#             (the j<0 perturbation is the supervisor's).
#  15.  P10   the push child's argv (dir8's clean push): the M2 -c flags,
#             -c core.hooksPath=/dev/null, -c credential.helper= (the reset),
#             then -c credential.helper=<ABS-JICHI-HARNESS> credential-helper
#             (the two-word spec; <ABS-JICHI-HARNESS> is the harness's own
#             path via readlink /proc/self/exe), then push -q <stand-in
#             absolute path> (NOT `origin`), the two refspecs ($BR/main,
#             $BR/work/*), NO --force, NO refs/kept/* refspec (the keep-gate
#             default). Covers behaviour 9 (no hooks), 10 (reset-then-set),
#             2 (absolute URL not origin), 12 (no --force), 15 (keep-gate).
#  16.  P2b   the global insteadOf is NEUTRALISED, not detected (dir): a
#             poisoned GLOBAL gitconfig (via a subshell GIT_CONFIG_GLOBAL for
#             the PUSH run only) with an insteadOf that redirects the stored
#             URL -- invisible to BOTH the guard's get-url child and the push
#             child (both carry the harness's empty GIT_CONFIG_GLOBAL) -> exit
#             0, the stand-in's $BR/main ADVANCES to the mirror's tip, the
#             redirected repo STAYS EMPTY (a port that pushed the redirected
#             URL is red).
#  17.  P11   the credential helper (direct, no push): `credential-helper get`
#             with JICHI_HARNESS_GIT_TOKEN_FILE=<token file>,
#             JICHI_HARNESS_CRED_RULE=oauth2 and JICHI_HARNESS_CRED_URL=<the
#             stored URL> in the env, fed git's credential-input identity on
#             stdin (protocol=file, EMPTY host AND EMPTY path -- a local
#             path's URL has no host; the path is part of the identity
#             (useHttpPath=true) and compared too, in git's form, cells 18-19)
#             prints exactly `username=oauth2` then `password=<canary>`.
#             The contract (cells 17-25): on `get` the helper requires the
#             stdin identity to MATCH the configured remote URL (file: protocol
#             =file, host EMPTY); any other identity is refused (exit 1, NO
#             password= line, the refusal on stderr) -- the race-free layer
#             against a check-then-use race in the URL guard.
#  18.  P11g  the PATH GUARD (F2): a SCHEME CRED_URL
#             (https://git.example.org/drive/remote.git), the token file at the
#             canary, the protocol AND host MATCH, the stdin path is a
#             DIFFERENT project (other/project.git -- git's form, no leading
#             slash, as useHttpPath=true sends it) -> REFUSED exit 1, NO
#             password= line on stdout, and the refusal names the configured
#             remote AND the requested path. The path is compared, not voided
#             (the same-uid redirect the identity guard holds with
#             useHttpPath=true).
#  19.  P11h  the MATCHING scheme cell (F2's control): the same SCHEME
#             CRED_URL, the protocol + host + path ALL match (path =
#             drive/remote.git, the url's path part byte-exact in GIT'S form --
#             no leading slash, as useHttpPath=true sends it) -> exit 0,
#             prints username=oauth2 then password=<canary>. Its stdout is the
#             second designed exception of the cell 29 sweep (the credential IS
#             the helper's output channel).
#  20.  P11b  `credential-helper store` and `erase` print NOTHING and exit 0.
#  21.  P11c  a MISSING token file is exit 1 (the refusal named).
#  22.  P11d  a token file shorter than 12 bytes is exit 1 (could not be
#             loaded).
#  23.  P11e  jichi-harness wrapped in a tiny argv-dump shim: the canary is
#             NOT an argument to the helper (it is in the env value + output
#             only).
#  24.  P11f  the HOST GUARD (the new 01:37 item 2 cell): `credential-helper
#             get` fed a DIFFERENT host on stdin (host=another) while
#             JICHI_HARNESS_CRED_URL=<the stored URL> -> exit 1, NO `password=
#             ` line on stdout, the refusal on stderr NAMES the remote it will
#             not serve (the stored URL).
#  25.  P11f2 the HOST guard ISOLATED (SV:1126): a scheme CRED_URL, the
#             protocol AND path match (path in git's form, no leading slash),
#             only the host differs -> refused
#             exit 1, no password= line, the isolation the existing
#             host-guard cell (P11f) lacked because its request also
#             changed the path.
#  27.  P1ls  the ls-remote child's argv + env (P1's tooth, 06:37 review):
#             a normal push (a clean dir11 fixture) under the git wrapper;
#             the ls-remote child (the key-scan exclusion) is the argv block
#             that carries `ls-remote`, and it must show, in order: -c
#             credential.helper= (the RESET), then -c
#             credential.helper=<ABS-JICHI-HARNESS> credential-helper (the
#             SET), then -c credential.useHttpPath=true; and its env dump
#             carries JICHI_HARNESS_GIT_TOKEN_FILE + JICHI_HARNESS_CRED_RULE
#             + JICHI_HARNESS_CRED_URL (the three NAMES; a local push's rule
#             is `none`). Pre-fix the ls-remote child had only the RESET +
#             useHttpPath + CRED_URL, so over HTTPS it had no helper and the
#             key scan refused fail-closed -- every real push refused.
#  28.  P7    the pushKept gate (P7's tooth, 06:37 review): a ref
#             refs/kept/planted is planted in the MIRROR (git update-ref after
#             the fetch), and (a) default (no pushKept key) -> push exit 0,
#             the stand-in has NO refs/heads/$BR/kept/planted (the gate is
#             OFF, nothing kept is pushed); (b) pushKept=true -> same plant,
#             push exit 0, the stand-in HAS refs/heads/$BR/kept/planted at the
#             same oid. The port of mirror.sh's ZD_PUSH_KEPT=1, now a real
#             config key with a real default, not an absent one.
#  26.  P12   the non-FF push is a FAILURE (dir9, clean history so the key
#             scan is silent): the stand-in's $BR/main gets a diverged commit
#             pushed directly to it while the mirror's $BR/main has its own
#             new commit -> `mirror push` exits 1 with git's `(fetch first)`
#             refusal captured, stand-in UNCHANGED. A port that tolerates a
#             rejected push (exit 0) is red.
#  29.  P17   the canary never leaks (FINAL cell, holds across ALL cells):
#             grep BOTH the token canary and the key canary across every
#             output/err file the driver produced AND the argv/env dump
#             file(s) -- assert NEITHER appears anywhere (the secret rule,
#             behaviour 15). The one designed exception is the helper's own
#             stdout (the credential IS the output channel, by design) -- the
#             sweep covers every capture except the helper's stdout captures
#             (p15, p19, p19h and p31a -- cell 31 leg a's fill capture, the
#             credential IS the helper's output channel), where it MUST be
#             present. Cell 6 (the isolated token-canary refusal,
#             P3c) names a file PATH, so the token canary is asserted ABSENT
#             from its output inside the cell AND covered by this sweep.
#  31.  P11r  REAL git as the credential peer (M1061, the red-first the
#             hand-written cells could not give): `git credential fill` with
#             -c credential.helper= (the reset) + -c
#             credential.helper=<ABS-JICHI-HARNESS> credential-helper (the
#             two-word spec, exactly jc_harness_push.c's credential_args) +
#             -c credential.useHttpPath=true, fed a `url=` line (offline: a
#             fill that the helper answers prints the identity + the helper's
#             answer and exits 0 without dialing; a fill the helper refuses
#             exits 128 with `fatal: could not read Username ... terminal
#             prompts disabled`). (a) the configured remote
#             (url=https://git.example.org/drive/remote.git, CRED_URL the
#             same, the token file + rule=oauth2 in the env): git exit 0, the
#             capture carries REAL git's identity lines (protocol=https,
#             host=git.example.org, path=drive/remote.git -- NO leading
#             slash, git's form under useHttpPath=true), username=oauth2 and
#             a password= line (the canary in this capture is a designed
#             sweep exception, the credential IS the helper's output
#             channel); (b) a DIFFERENT project on the same host
#             (url=.../other/project.git): git exit 128, NO password= line,
#             the helper's path-refusal on stderr naming the configured
#             remote -- the guard's teeth are not loosened (green before and
#             after the M1061 fix). On the unfixed tree (a) is RED: the
#             helper classifies the configured remote's path WITH its
#             leading '/' and compares byte-exactly, so it refuses git's
#             slashless path of its OWN remote -- git exit 128, 0 password
#             lines, the "requested path does not match" line.
#
# NOT a driver cell: the master guard (behaviour 12) is a source-review
# property (the port builds refspecs internally from the branch prefix and
# never emits refs/heads/master); the note says so explicitly.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"

t_plan 32
smoke_home
tmp=$(smoke_tmp)

# smoke_lint check 5: this non-lint driver runs the tree's "$BIN" once; the
# fixture git children are neutralised, but $BIN must be exercised so the lint
# sees the driver validate the just-built binary, not a PATH jichi.
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
TOKEN_CANARY="CANARY-TOKEN-DO-NOT-PRINT"

# A fixture failure emits one t_fail per cell so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt 32 ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

if [ ! -x "$HBIN" ]; then
    fixture_fail "the harness binary $HBIN is not built (run `make jichi-harness` first)"
fi

# --- the git wrapper: argv + env capture ------------------------------------
# mkwrapper DIR makes DIR a PATH entry holding a `git` script that appends an
# "ARGV:" marker, the child's full argv (one argument per line, up to the
# "ENDARGV:" terminator), then the child's environment (up to the next "ARGV:")
# to DIR/dump, and execs the REAL git (resolved at runtime -- the driver never
# assumes /usr/bin). Use PATH="$DIR:$PATH" and read the capture from DIR/dump.
_realgit=$(command -v git)

mkwrapper() {
    _ww_d="$1"
    mkdir -p "$_ww_d"
    printf '#!/bin/sh\n{ printf "ARGV:\\n"; for _a in "$@"; do printf "%%s\\n" "$_a"; done; printf "ENDARGV:\\n"; env; } >> %s/dump\nexec %s "$@"\n' \
        "$_ww_d" "$_realgit" > "$_ww_d/git"
    chmod 755 "$_ww_d/git"
}

# pushargv DIR -- the push child's argv from a wrapper dump (DIR/dump), joined
# with a single space. The push child is the git argv block that carries a
# standalone `push` argument (the harness runs `git -c ... -C <repo> push -q
# ...`). The URL-guard block is `remote get-url --push origin`, whose `--push`
# has a dash before it, so the " push " word match skips it. The ENDARGV:
# terminator keeps a multi-line ENV value out of the argv. No fixture path
# carries a space, so the single-space join is safe for the word-matches below.
pushargv() {
    awk '
        /^ARGV:$/           { acc = ""; inb = 1; next }
        inb && /^ENDARGV:$/ { inb = 0; if (acc ~ " push ") print acc; next }
        inb                 { acc = acc " " $0 }
    ' "$1/dump" 2>/dev/null
}

# lsargv DIR -- the ls-remote child's argv from a wrapper dump (DIR/dump),
# joined with a single space. The ls-remote child is the git argv block that
# carries a standalone `ls-remote` argument (the harness's key-scan exclusion
# runs `git -c ... -C <repo> ls-remote <stored-url>`). The ENDARGV: terminator
# keeps a multi-line ENV value out of the argv.
lsargv() {
    awk '
        /^ARGV:$/           { acc = ""; inb = 1; next }
        inb && /^ENDARGV:$/ { inb = 0; if (acc ~ " ls-remote ") print acc; next }
        inb                 { acc = acc " " $0 }
    ' "$1/dump" 2>/dev/null
}

# lsenv DIR -- the environment the wrapper dumped AFTER the ls-remote child's
# ENDARGV and BEFORE the next child's ARGV (the ls-remote child's own env, the
# cell 27 assertion target). Prints each env line (NAME=value).
lsenv() {
    awk '
        /^ARGV:$/           { inb = 1; acc = ""; inenv = 0; next }
        inb && /^ENDARGV:$/ { inb = 0; inenv = (acc ~ " ls-remote "); next }
        inb                 { acc = acc " " $0 }
        inenv               { print $0 }
    ' "$1/dump" 2>/dev/null
}

# --- the fixtures: the M1055 rehearsal layout, four times --------------------
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

# The harness config's canary files: the key is RANDOM (it must never be a
# fixed string -- the reference's 2026-09-28 incident was a fixed one), the
# token is a NAMED canary so the P17 leak cell can grep for it.
_kc=$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n'); [ -n "$_kc" ] || _kc="keycanary$$_"
printf '%s\n' "$_kc" > "$tmp/kreal"; chmod 600 "$tmp/kreal"
printf '%s\n' "$TOKEN_CANARY" > "$tmp/tokfile"; chmod 600 "$tmp/tokfile"

# mkrehearsal NAME -- init a fresh rehearsal at $tmp/<NAME> from $src, store
# the mirror's config copy (on the fixed tree init writes it itself; on the
# red tree the driver writes it, so the M1055 checks can run), and add the two
# $BR/work/* branches with distinct tips so movement is trackable.
mkrehearsal() {
    _mr="$1"
    cat > "$tmp/cfg$_mr.json" <<EOF
{"dir":"$tmp/dir$_mr",
"drive":{"branchPrefix":"$BR","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"modelKeyFile":"$tmp/kreal","gitTokenFile":"$tmp/tokfile"}
EOF
    "$HBIN" init --config "$tmp/cfg$_mr.json" rehearsal "$tmp/dir$_mr" \
        --from "$src" --jichi-bin "$tmp/jichi" \
        < /dev/null > "$tmp/i$_mr" 2> "$tmp/i$_mr.err" || return 1
    [ -f "$tmp/dir$_mr/mirror.git-config.harness-copy" ] || \
        cp "$tmp/dir$_mr/mirror.git/config" "$tmp/dir$_mr/mirror.git-config.harness-copy"
    git -C "$tmp/dir$_mr/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
        commit -q --allow-empty -m "team$_mr: main c2" || return 1
    git -C "$tmp/dir$_mr/team" branch -q "$BR/work/a" "$BR/main" || return 1
    git -C "$tmp/dir$_mr/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
        commit -q --allow-empty -m "team$_mr: work/a" || return 1
    git -C "$tmp/dir$_mr/team" branch -q "$BR/work/b" || return 1
    git -C "$tmp/dir$_mr/team" checkout -q "$BR/main" || return 1
}

# hfetch NAME -- M1055's `mirror fetch` (built), to populate the mirror.
hfetch() {
    "$HBIN" mirror fetch --config "$tmp/cfg$1.json" "$tmp/dir$1/mirror.git" \
        < /dev/null > "$tmp/f$1" 2> "$tmp/f$1.err"
}

mkrehearsal "" || fixture_fail "init (dir) failed: $(tr '\n' ' ' < "$tmp/i" | head_bytes 160)"
hfetch "" || fixture_fail "mirror fetch (dir) failed: $(tr '\n' ' ' < "$tmp/f" | head_bytes 160)"
mkrehearsal "8" || fixture_fail "init (dir8) failed: $(tr '\n' ' ' < "$tmp/i8" | head_bytes 160)"
hfetch "8" || fixture_fail "mirror fetch (dir8) failed: $(tr '\n' ' ' < "$tmp/f8" | head_bytes 160)"
mkrehearsal "2" || fixture_fail "init (dir2) failed: $(tr '\n' ' ' < "$tmp/i2" | head_bytes 160)"
hfetch "2" || fixture_fail "mirror fetch (dir2) failed: $(tr '\n' ' ' < "$tmp/f2" | head_bytes 160)"
mkrehearsal "9" || fixture_fail "init (dir9) failed: $(tr '\n' ' ' < "$tmp/i9" | head_bytes 160)"
hfetch "9" || fixture_fail "mirror fetch (dir9) failed: $(tr '\n' ' ' < "$tmp/f9" | head_bytes 160)"
mkrehearsal "10" || fixture_fail "init (dir10) failed: $(tr '\n' ' ' < "$tmp/i10" | head_bytes 160)"
hfetch "10" || fixture_fail "mirror fetch (dir10) failed: $(tr '\n' ' ' < "$tmp/f10" | head_bytes 160)"
mkrehearsal "11" || fixture_fail "init (dir11) failed: $(tr '\n' ' ' < "$tmp/i11" | head_bytes 160)"
hfetch "11" || fixture_fail "mirror fetch (dir11) failed: $(tr '\n' ' ' < "$tmp/f11" | head_bytes 160)"
mkrehearsal "12" || fixture_fail "init (dir12) failed: $(tr '\n' ' ' < "$tmp/i12" | head_bytes 160)"
hfetch "12" || fixture_fail "mirror fetch (dir12) failed: $(tr '\n' ' ' < "$tmp/f12" | head_bytes 160)"
# cfg11b.json: dir11's config with the pushKept gate ON (cell 28b). cfg11.json
# (mkrehearsal's) has NO pushKept key -- the DEFAULT (off) is the cell 28a
# round, so the gate's two states are measured on the SAME rehearsal.
cat > "$tmp/cfg11b.json" <<EOF
{"dir":"$tmp/dir11",
"drive":{"branchPrefix":"$BR","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"modelKeyFile":"$tmp/kreal","gitTokenFile":"$tmp/tokfile","pushKept":true}
EOF

# The mirror's origin URL as the harness stored it (the push destination;
# read from the stored config copy, not the live config).
stored_url=$(awk '/^\[remote "origin"\]/{f=1} f && /^[ \t]*url *=/{sub(/^[ \t]*url *= */,""); print; exit}' "$tmp/dir/mirror.git-config.harness-copy")
[ -n "$stored_url" ] || stored_url="$tmp/dir/standin.git"

# --- 1: P1 -- the happy push (control) ---------------------------------------
# dir's mirror is clean (no canary, no big blob), so the port's gates pass and
# the push LANDS. Exit 0, the stand-in's $BR/main + $BR/work/* equal the
# mirror's, and the push child's argv carries the stand-in's ABSOLUTE path,
# NOT `origin`, NO --force.
mkwrapper "$tmp/d1"
PATH="$tmp/d1:$PATH" "$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/p1" 2> "$tmp/p1.err"
rc1=$?
_ln1=$(pushargv "$tmp/d1")
if [ "$rc1" -ne 0 ]; then
    t_fail "1: P1 the happy push exited $rc1 (want 0) -- on the unfixed tree \`mirror push\` is the usage refusal: $(tr '\n' ' ' < "$tmp/p1.err" | head_bytes 200)"
elif [ -z "$_ln1" ]; then
    t_fail "1: P1 the push exited 0 but the git wrapper captured NO push argv -- the instrument never ran"
else
    _smain=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
    _stmain=$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
    _swa=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$BR/work/a" 2>/dev/null)
    _stw_a=$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/work/a" 2>/dev/null)
    if [ -z "$_stmain" ] || [ "$_stmain" != "$_smain" ] || [ -z "$_stw_a" ] || [ "$_stw_a" != "$_swa" ]; then
        t_fail "1: P1 the push exited 0 but did not LAND (stand-in main '$_stmain' vs mirror '$_smain', work/a '$_stw_a' vs '$_swa')"
    elif ! printf '%s\n' "$_ln1" | grep -qF "$tmp/dir/standin.git"; then
        t_fail "1: P1 the push child's argv does not carry the stand-in's ABSOLUTE path ($tmp/dir/standin.git): $_ln1"
    elif printf '%s\n' "$_ln1" | grep -qw origin; then
        t_fail "1: P1 the push child's argv carries the bare \`origin\` name (behaviour 2: never the origin name): $_ln1"
    elif printf '%s\n' "$_ln1" | grep -q -- --force; then
        t_fail "1: P1 the push child's argv carries --force: $_ln1"
    else
        t_ok "1: P1 the happy push exits 0, the stand-in's $BR/main + $BR/work/* equal the mirror's (LANDED), and the push child's argv carries the stand-in's absolute path (not \`origin\`) with no --force"
    fi
fi

# --- 2: P2 -- the MIRROR-LOCAL plants, caught by CHECK 3 (git-free) --------
# A `remote set-url origin <a different bare repo>` / a `pushurl` plant changes
# the LIVE config bytes (the stored copy is untouched). Under the F1 order all
# FIVE precheck checks complete BEFORE any git child, so the git-free CHECK 3
# (stored-copy byte comparison) fires FIRST and refuses the plant on config
# bytes -- exit 2, the stand-in UNCHANGED, and NO git child at all (check 3
# needs no git, so the wrapper dump is ABSENT: not a get-url, not a push).
# The guard's DISTINCT tooth (a plant check 3 CANNOT see, because live==stored
# bytes) is cell 3 (P2c). On the red tree the exit is 2 too, but the ABSENCE
# of a git child plus the "check 3" name distinguishes this from the guard.
git init -q --bare "$tmp/other.git"
_s_before2=$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
git -C "$tmp/dir/mirror.git" remote set-url origin "$tmp/other.git"
mkwrapper "$tmp/d2a"
PATH="$tmp/d2a:$PATH" "$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/p2a" 2> "$tmp/p2a.err"
rc2a=$?
git -C "$tmp/dir/mirror.git" remote set-url origin "$stored_url"
git -C "$tmp/dir/mirror.git" config remote.origin.pushurl "$tmp/other.git"
mkwrapper "$tmp/d2b"
PATH="$tmp/d2b:$PATH" "$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/p2b" 2> "$tmp/p2b.err"
rc2b=$?
git -C "$tmp/dir/mirror.git" config --unset remote.origin.pushurl 2>/dev/null
if [ "$rc2a" -ne 2 ] || [ "$rc2b" -ne 2 ]; then
    t_fail "2: P2 the mirror-local plants were not refused: set-url exit $rc2a (want 2), pushurl exit $rc2b (want 2): $(tr '\n' ' ' < "$tmp/p2a.err" | head_bytes 120) / $(tr '\n' ' ' < "$tmp/p2b.err" | head_bytes 120)"
elif ! grep -qF "check 3" "$tmp/p2a.err" || ! grep -qF "check 3" "$tmp/p2b.err"; then
    t_fail "2: P2 the local set-url/pushurl plant was not caught by CHECK 3 (the git-free config-bytes check, F1: all five checks before any git child): $(tr '\n' ' ' < "$tmp/p2a.err" | head_bytes 120) / $(tr '\n' ' ' < "$tmp/p2b.err" | head_bytes 120)"
elif [ "$_s_before2" != "$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)" ]; then
    t_fail "2: P2 check 3 refused the plant but the stand-in's $BR/main CHANGED -- the refusal must fire before any push"
elif [ -n "$(pushargv "$tmp/d2a")" ] || [ -n "$(pushargv "$tmp/d2b")" ]; then
    t_fail "2: P2 check 3 is git-free, yet a push child STILL RAN: $(pushargv "$tmp/d2a")"
else
    t_ok "2: P2 a planted \`remote set-url origin\` AND a \`pushurl\` (both change the LIVE config bytes, the stored copy untouched) are refused by CHECK 3 (git-free, exit 2, before any git child), the stand-in's refs UNCHANGED, and NO git child ran -- the guard's DISTINCT tooth (check 3 cannot see it) is cell 3 (P2c)"
fi


# --- 3: P2c -- the URL guard's DISTINCT tooth, WITH check 3 satisfied (F1) --
# The review's F1: a mirror-local set-url/pushurl plant changes the LIVE
# config, so after the F1 fix CHECK 3 refuses it before the guard's get-url
# child runs -- the guard is redundant-but-present for those plants, and its
# distinct tooth is a plant check 3 cannot see. Construction: AFTER the cell
# 2 restores (live == stored, so init's bytes hold), rewrite the URL to
# $tmp/other.git in BOTH the live config AND the stored copy (same text
# substitution: the url = line), so the config BYTES still match (check 3
# passes) while the stored URL ($tmp/other.git) no longer equals the
# EFFECTIVE push URL. A pushurl pointing at the stand-in (also in BOTH
# files, so the bytes still match) makes get-url --push return the stand-in:
# stored URL $tmp/other.git != effective $stored_url -> the URL guard alone
# refuses, exit 2 naming both, BEFORE any ls-remote/push child (a get-url
# block is in the wrapper dump, NO standalone push argv, the stand-in's
# refs UNCHANGED). The live config is restored by the cell 16 (P2b)
# pushurl plant's own unset (the harness rewrites the file, then the unset
# restores it).
_s_before3c=$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
# The SAME text substitution on BOTH files (git writes a TAB-indented
# `url =` line, so the awk matches `^[ \t]*url =`): the url line -> other.git,
# and a pushurl -> the stand-in (so the EFFECTIVE push URL stays the stand-in).
# The bytes stay EQUAL (check 3 passes); only the stored URL moves.
# $1 = the new url (other.git), $2 = the file to rewrite, $3 = the pushurl
# (the stand-in, so the EFFECTIVE push URL stays the stand-in). Only $2 is
# read as awk input; $1/$3 are passed by value (-v). The pushurl is printed
# AFTER the url line, so it lands INSIDE the [remote "origin"] section.
_awk_plant() { awk -v u="$1" -v pu="$3" '
    /^[ \t]*url *=/ && !seen { sub(/url *=.*/, "url = " u); seen = 1; print "\tpushurl = " pu }
    /^[ \t]*pushurl *=/      { next }
    { print }' "$2" > "$2.new" && mv "$2.new" "$2"; }
_awk_plant "$tmp/other.git" "$tmp/dir/mirror.git/config" "$stored_url"
_awk_plant "$tmp/other.git" "$tmp/dir/mirror.git-config.harness-copy" "$stored_url"
# fixture guard: the url line WAS rewritten and the bytes still match.
if ! grep -qF "url = $tmp/other.git" "$tmp/dir/mirror.git/config" \
    || ! grep -qF "url = $tmp/other.git" "$tmp/dir/mirror.git-config.harness-copy" \
    || ! cmp -s "$tmp/dir/mirror.git/config" "$tmp/dir/mirror.git-config.harness-copy"; then
    t_fail "3: P2c the fixture is wrong: the identical rewrite (url = $tmp/other.git in BOTH the live config and the stored copy, plus pushurl = $stored_url) did not hold -- the bytes must stay EQUAL (check 3 passes) while the stored URL moves: $(grep -hE 'url *=|pushurl *=' "$tmp/dir/mirror.git/config" | head_bytes 160)"
else
    mkwrapper "$tmp/d3c"
    PATH="$tmp/d3c:$PATH" "$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
        < /dev/null > "$tmp/p3c" 2> "$tmp/p3c.err"
    rc3c=$?
    _ln3c=$(pushargv "$tmp/d3c")
    if [ "$rc3c" -ne 2 ]; then
        t_fail "3: P2c the URL guard (WITH check 3 satisfied: the stored copy and the live config both carry the wrong url = $tmp/other.git, so the config bytes still match) exited $rc3c (want 2): $(tr '\n' ' ' < "$tmp/p3c.err" | head_bytes 200)"
    elif ! grep -qF "URL guard" "$tmp/p3c.err"; then
        t_fail "3: P2c the refusal is not the URL guard's own (check 3 or another check fired first): $(tr '\n' ' ' < "$tmp/p3c.err" | head_bytes 200)"
    elif [ "$_s_before3c" != "$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)" ]; then
        t_fail "3: P2c the guard refused but the stand-in's $BR/main CHANGED -- the guard must fire before any push"
    elif [ -n "$_ln3c" ]; then
        t_fail "3: P2c the guard refused but a push child STILL RAN -- the guard must refuse before the push child: $_ln3c"
    elif [ ! -e "$tmp/d3c/dump" ] || ! grep -q "get-url" "$tmp/d3c/dump"; then
        t_fail "3: P2c the guard exited 2 but its get-url child is not in the wrapper dump -- the guard must compute the effective push URL before refusing: $(tr '\n' ' ' < "$tmp/p3c.err" | head_bytes 120)"
    else
        t_ok "3: P2c the URL guard's DISTINCT tooth: with check 3 SATISFIED (the stored copy and the live config rewritten IDENTICALLY to url = $tmp/other.git, so the config bytes still match), the guard's get-url child catches stored URL != effective push URL -- exit 2, the guard named, the stand-in UNCHANGED, no push child (the mirror-local plants of cell 2 are refused by check 3 FIRST, git-free; the guard is redundant-but-present for them, and this is the plant only it can see)"
    fi
fi
# restore BOTH files (cells 7-16 reuse dir; the live config is what the next
# push's check 3 compares, and $stored_url is re-read from the stored copy at
# the top of the driver -- it already holds the stand-in, and this restores the
# live config's url + removes the planted pushurl, then re-syncs the copy):
git -C "$tmp/dir/mirror.git" config --replace-all remote.origin.url "$stored_url"
git -C "$tmp/dir/mirror.git" config --unset remote.origin.pushurl 2>/dev/null
cp "$tmp/dir/mirror.git/config" "$tmp/dir/mirror.git-config.harness-copy"
# --- 4: P3 -- the key scan (a secret in a blob the push adds) ----------------
# dir2: commit a model-key-canary file in team, fetch it into the mirror,
# push -> refused exit 1 naming the file PATH (not its content), stand-in
# UNCHANGED.
printf '%s\n' "$_kc" > "$tmp/dir2/team/keyfile"
git -C "$tmp/dir2/team" add keyfile
git -C "$tmp/dir2/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team2: add keyfile (canary)"
hfetch "2"
_s_before3=$(git -C "$tmp/dir2/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
"$HBIN" mirror push --config "$tmp/cfg2.json" "$tmp/dir2/mirror.git" \
    < /dev/null > "$tmp/p3" 2> "$tmp/p3.err"
rc3=$?
if [ "$rc3" -ne 1 ]; then
    t_fail "4: P3 the key scan did not refuse the canary-bearing push (exit $rc3, want 1): $(tr '\n' ' ' < "$tmp/p3.err" | head_bytes 200)"
elif ! grep -qF "keyfile" "$tmp/p3" "$tmp/p3.err"; then
    t_fail "4: P3 the key scan refused but does not name the offending file PATH (keyfile): $(tr '\n' ' ' < "$tmp/p3.err" | head_bytes 200)"
elif grep -qF "$_kc" "$tmp/p3" "$tmp/p3.err"; then
    t_fail "4: P3 the key scan REFUSED but printed the canary CONTENT (only the path may be named)"
elif [ "$_s_before3" != "$(git -C "$tmp/dir2/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)" ]; then
    t_fail "4: P3 the key scan refused but the stand-in's $BR/main CHANGED -- the push must not happen"
else
    t_ok "4: P3 the key scan refuses a push whose added blob carries the model-key canary, exit 1, naming the file PATH (not its content), the stand-in UNCHANGED"
fi

# --- 5: P3b -- control: canary EMBEDDED in surrounding text ------------------
printf 'x%sy\n' "$_kc" > "$tmp/dir2/team/keyembed"
git -C "$tmp/dir2/team" add keyembed
git -C "$tmp/dir2/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team2: add keyembed (canary embedded)"
hfetch "2"
"$HBIN" mirror push --config "$tmp/cfg2.json" "$tmp/dir2/mirror.git" \
    < /dev/null > "$tmp/p4" 2> "$tmp/p4.err"
rc4=$?
if [ "$rc4" -ne 1 ]; then
    t_fail "5: P3b control the embedded canary (x<canary>y) was NOT refused (exit $rc4, want 1) -- the scan must be a substring match, not exact-token"
elif ! grep -qF "keyembed" "$tmp/p4" "$tmp/p4.err"; then
    t_fail "5: P3b control the refusal does not name the embedded file PATH (keyembed): $(tr '\n' ' ' < "$tmp/p4.err" | head_bytes 200)"
else
    t_ok "5: P3b control a canary EMBEDDED in surrounding text (x<canary>y) is ALSO refused, exit 1, naming the path (the reference's grep -F is a substring search)"
fi

# --- 6: P3c -- ISOLATED token-canary perturbation (06:37 review fix) ---------
# A FRESH rehearsal (dir10) whose ONLY secret is the token canary: commit a
# file whose content is exactly the token canary, then a second commit that
# DELETES it (added-then-deleted before the tip -- behaviour 4's 2026-09-28
# incident: the push's range `rev-list --objects tips... ^excluded --not
# --remotes=origin` reaches the added-then-deleted blob through history). The
# refusal must name the FILE PATH (tokhist) and NEVER the canary content.
# Isolated on dir10 (not dir2): dir2's range still carries cell 4's model-key
# canary, so a token canary committed into dir2 would be refused by the KEY
# scan branch even if the TOKEN branch were disabled (the 06:37 finding: the
# old cell 5 was vacuous). dir10 has no key canary, so the ONLY thing that can
# refuse it is the token scan branch -- the cell is a real instrument for it.
printf '%s\n' "$TOKEN_CANARY" > "$tmp/dir10/team/tokhist"
git -C "$tmp/dir10/team" add tokhist
git -C "$tmp/dir10/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team10: add tokhist (token canary)"
git -C "$tmp/dir10/team" rm -q tokhist
git -C "$tmp/dir10/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team10: delete tokhist"
hfetch "10"
"$HBIN" mirror push --config "$tmp/cfg10.json" "$tmp/dir10/mirror.git" \
    < /dev/null > "$tmp/p5" 2> "$tmp/p5.err"
rc5=$?
if [ "$rc5" -ne 1 ]; then
    t_fail "6: P3c (isolated) the added-then-deleted TOKEN canary was NOT refused (exit $rc5, want 1) -- dir10's only secret is the token canary, so the token scan branch must reach the blob through history"
elif ! grep -qF "tokhist" "$tmp/p5" "$tmp/p5.err"; then
    t_fail "6: P3c (isolated) the token-scan refusal does not name the offending file PATH (tokhist): $(tr '\n' ' ' < "$tmp/p5.err" | head_bytes 200)"
elif grep -qF "$TOKEN_CANARY" "$tmp/p5" "$tmp/p5.err"; then
    t_fail "6: P3c (isolated) the token-scan refusal printed the TOKEN CANARY CONTENT (only the file path may be named, the secret rule)"
else
    t_ok "6: P3c (isolated, 06:37 fix) a token canary in a file ADDED in an earlier commit and DELETED before the tip is still refused, exit 1, on a FRESH fixture (dir10) whose only secret is the token canary -- the token scan branch (not the key canary, absent here) is what refuses it; names the path, never the content"
fi

# --- 7: P3d -- fail-closed: gitTokenFile missing -> "token could not be loaded"
# Point dir's config gitTokenFile at a MISSING file. The scan cannot load the
# token, so it fails closed: refused exit 1, the missing secret named, no push
# attempted. (The port loads both secrets before scanning; a token it cannot
# load means it cannot check, which is a refusal, never a pass.)
sed 's#/tokfile"#/no-such-token-file"#' "$tmp/cfg.json" > "$tmp/cfg6.json"
"$HBIN" mirror push --config "$tmp/cfg6.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/p6" 2> "$tmp/p6.err"
rc6=$?
if [ "$rc6" -ne 1 ]; then
    t_fail "7: P3d fail-closed a MISSING gitTokenFile was not refused (exit $rc6, want 1) -- the scan must fail closed, never exit 0"
elif ! grep -qiE "token" "$tmp/p6" "$tmp/p6.err"; then
    t_fail "7: P3d fail-closed the refusal does not name the missing token: $(tr '\n' ' ' < "$tmp/p6.err" | head_bytes 200)"
else
    t_ok "7: P3d fail-closed a missing gitTokenFile is refused exit 1 naming the token (the scan cannot run), and no push is attempted"
fi

# --- 8: P4 -- the size check (a 9 MB blob in team) ---------------------------
# dir is clean of canaries, so the size verdict is the one to read.
dd if=/dev/zero of="$tmp/dir/team/large9.bin" bs=1048576 count=9 2>/dev/null
git -C "$tmp/dir/team" add large9.bin
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team: add large9.bin (9 MB)"
hfetch ""
_s_before7=$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
"$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/p7" 2> "$tmp/p7.err"
rc7=$?
if [ "$rc7" -ne 1 ]; then
    t_fail "8: P4 the 9 MB blob was not refused (exit $rc7, want 1): $(tr '\n' ' ' < "$tmp/p7.err" | head_bytes 200)"
elif ! grep -qF "large9.bin" "$tmp/p7" "$tmp/p7.err"; then
    t_fail "8: P4 the size check refused but does not name the offending file (large9.bin): $(tr '\n' ' ' < "$tmp/p7.err" | head_bytes 200)"
elif ! grep -qiE "MB" "$tmp/p7" "$tmp/p7.err"; then
    t_fail "8: P4 the size check refused but does not report the size in MB: $(tr '\n' ' ' < "$tmp/p7.err" | head_bytes 200)"
elif [ "$_s_before7" != "$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)" ]; then
    t_fail "8: P4 the size check refused but the stand-in's $BR/main CHANGED -- the push must not happen"
else
    t_ok "8: P4 the size check refuses a 9 MB blob (above the 8 MB per-file limit), exit 1, naming the file and its size in MB, the stand-in UNCHANGED"
fi

# --- 9: P4b -- control: a 1 MB blob pushes clean (exit 0, landed) -----------
# Measured on dir8 (the FRESH mirror whose history is clean), because dir's
# history now carries cell 7's 9 MB blob -- and a blob added and still in the
# tip is in the push's range regardless of later deletions, so dir cannot land
# a clean 1 MB push.
dd if=/dev/zero of="$tmp/dir8/team/small1.bin" bs=1048576 count=1 2>/dev/null
git -C "$tmp/dir8/team" add small1.bin
git -C "$tmp/dir8/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team8: add small1.bin (1 MB, under the limit)"
hfetch "8"
_sm8_main=$(git -C "$tmp/dir8/mirror.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
"$HBIN" mirror push --config "$tmp/cfg8.json" "$tmp/dir8/mirror.git" \
    < /dev/null > "$tmp/p8" 2> "$tmp/p8.err"
rc8=$?
if [ "$rc8" -ne 0 ]; then
    t_fail "9: P4b control a 1 MB blob did not push clean (exit $rc8, want 0): $(tr '\n' ' ' < "$tmp/p8.err" | head_bytes 200)"
elif [ "$(git -C "$tmp/dir8/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)" != "$_sm8_main" ]; then
    t_fail "9: P4b control the 1 MB push exited 0 but did not LAND"
else
    t_ok "9: P4b control a 1 MB blob (under the 8 MB per-file limit) pushes clean: exit 0, landed"
fi

# --- 32: P4b2 -- the WITHIN-LIMITS biggest-file number is one-decimal, never a
# whole-MB ceiling (card 99 / SV:1419, M1072) --------------------------------
# dir12 (a FRESH clean rehearsal, mirroring P4b's dir8 shape): commit ONE blob
# of 131072 bytes (0.13 MB, under 1 MB -- the same order of magnitude as the
# supervisor's push.log biggest blob, LOG-3.md 131581 bytes) on $BR/main, fetch,
# and push. Under the limit, so it LANDS (exit 0) and the WITHIN-LIMITS
# size-check line is printed on stdout. That line prints the TOTAL as %.1f MB
# and (the fix) the biggest file version as %.1f MB too -- so the biggest
# number can never be LARGER than the total (the total is the sum of every
# blob, the biggest is the max: sum >= max always). Pre-fix the biggest was a
# whole-MB CEILING (max_file_bytes/1MB rounded UP) printed %lu -- a 0.13 MB
# blob read "1 MB" next to a "0.1 MB" total, biggest > total on its own line.
# This cell extracts both numbers and asserts biggest <= total numerically:
# RED pre-fix (biggest=1 > total=0.1), GREEN post-fix (biggest=0.1 <= total=0.1).
dd if=/dev/zero of="$tmp/dir12/team/sub1.bin" bs=1024 count=128 2>/dev/null
git -C "$tmp/dir12/team" add sub1.bin
git -C "$tmp/dir12/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team12: add sub1.bin (0.13 MB, the within-limits biggest-file precision)"
hfetch "12"
"$HBIN" mirror push --config "$tmp/cfg12.json" "$tmp/dir12/mirror.git" \
    < /dev/null > "$tmp/p32" 2> "$tmp/p32.err"
rc32=$?
# The WITHIN-LIMITS line is on stdout (say() writes stdout). Isolate it from
# the over-limit lines ("over the limit") by the "within the limits" phrase.
_szline=$(grep -F "the size check:" "$tmp/p32" | grep -F "within the limits" | head -1)
# total: the number between "file version(s), " and " MB in all"; biggest: the
# number between "biggest file version " and " MB". Both are %.1f post-fix; the
# biggest is a whole integer ("1") pre-fix -- [0-9.]* captures both forms. Each
# tag ("file version(s), N MB in all", "biggest file version N MB") occurs ONCE
# on the line, so the greedy .*/[0-9.]* pair lands on the right number (the
# file COUNT precedes "file version(s)" and is not swallowed).
_sz_total=$(printf '%s\n' "$_szline" | sed -n 's/^.*: [0-9]* file version(s), \([0-9.]*\) MB in all,.*$/\1/p')
_sz_biggest=$(printf '%s\n' "$_szline" | sed -n 's/^.*the biggest file version \([0-9.]*\) MB --.*$/\1/p')
_sz_cmp=$(awk -v b="$_sz_biggest" -v t="$_sz_total" 'BEGIN{print (b+0<=t+0)?1:0}')
if [ "$rc32" -ne 0 ]; then
    t_fail "32: P4b2 the 0.13 MB blob (under the 8 MB per-file limit) did not push clean (exit $rc32, want 0): $(tr '\n' ' ' < "$tmp/p32.err" | head_bytes 200)"
elif [ -z "$_szline" ]; then
    t_fail "32: P4b2 the WITHIN-LIMITS size-check line is absent from the push's stdout -- the line that carries the biggest-file number: $(tr '\n' ' ' < "$tmp/p32" | head_bytes 240)"
elif [ -z "$_sz_total" ] || [ -z "$_sz_biggest" ]; then
    t_fail "32: P4b2 could not extract the total/biggest numbers from the size-check line (total='$_sz_total' biggest='$_sz_biggest'): $_szline"
elif [ "$_sz_cmp" -ne 1 ]; then
    t_fail "32: P4b2 the within-limits size check prints a biggest-file number LARGER than the total (biggest=$_sz_biggest MB > total=$_sz_total MB) -- the biggest must use the total's one-decimal formatter, not a whole-MB ceiling (card 99 / SV:1419): $_szline"
else
    t_ok "32: P4b2 a 0.13 MB blob pushes clean and the within-limits size check's biggest-file number never exceeds the total (biggest=$_sz_biggest MB <= total=$_sz_total MB, both one-decimal -- the whole-MB ceiling is gone, card 99 / SV:1419)"
fi

# --- 10: P4c -- the per-push TOTAL over (three distinct 7 MB files) ----------
# The real mechanism (06:37 review fix): the per-push limit is the sum of the
# SIZES of every file version the push adds. Three DISTINCT 7 MB files total
# 21 MB (> the 20 MB per-push limit) while EACH is 7 MB (< the 8 MB per-file
# limit) -- so the per-file check is silent and the per-PUSH total trips.
# (The old fixture, two identical /dev/zero 12 MB files, was ONE deduped blob
# plus the 9 MB blob already in history -- the total tripped for the wrong
# reason.) Each file is made distinct with a counter written first, so git
# cannot dedupe them; they are added in separate commits so the range carries
# three distinct 7 MB blob versions.
for _n in a b c; do
    dd if=/dev/urandom of="$tmp/dir/team/seven_${_n}.bin" bs=1048576 count=7 2>/dev/null
done
git -C "$tmp/dir/team" add seven_a.bin
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team: add seven_a.bin (7 MB)"
hfetch ""
git -C "$tmp/dir/team" add seven_b.bin
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team: add seven_b.bin (7 MB)"
hfetch ""
git -C "$tmp/dir/team" add seven_c.bin
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team: add seven_c.bin (7 MB)"
hfetch ""
"$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/p9" 2> "$tmp/p9.err"
rc9=$?
if [ "$rc9" -eq 0 ]; then
    t_fail "10: P4c the over-20 MB push LANDED (exit 0) -- the per-push limit must refuse it"
elif ! grep -qiE "MB in all|per push" "$tmp/p9" "$tmp/p9.err"; then
    t_fail "10: P4c the over-total push was refused but the per-push limit is not named (the reference's report: '<n> file version(s), <total> MB in all ... over the limit of ... 20 MB per push'): $(tr '\n' ' ' < "$tmp/p9.err" | head_bytes 200)"
else
    t_ok "10: P4c three DISTINCT 7 MB files (each under the 8 MB per-file limit; 21 MB in all, over the 20 MB per-push total) are refused exit 1, the per-push limit named"
fi

# --- 11: P4d -- perturbation: the big blobs already in the remote are NOT
# re-counted (the ls-remote exclusion). dir's stand-in only holds cell 1's
# refs (cells 7-9 were refused, so the 9 MB + 2x12 MB blobs are in the mirror
# but NOT the stand-in). Land them all with a DIRECT git push to the stand-in
# (the driver's own instrument) and DO NOT refresh the mirror's tracking
# refs (the port pushes by URL, so a push never advances refs/remotes/origin
# -- and the driver must not paper over that gap either): after this, the
# stand-in's tips sit PAST the mirror's stale tracking refs. THEN add one new
# 1 MB blob. The port's size range must exclude what the remote ALREADY HOLDS
# by `git ls-remote <stored URL>` (the port's own exclusion -- behaviour 4's
# 01:37 item 1, the tracking-refs gap): the added set is only the 1 MB blob,
# so the push LANDS (exit 0). A port that excludes ONLY via `--not
# --remotes=origin` re-counts the 9 MB blob (the tracking refs never moved)
# and is refused -- red. (dir's mirror history has no canary, so the key scan
# is silent.)
git -C "$tmp/dir/mirror.git" push -q "$tmp/dir/standin.git" \
    "refs/heads/$BR/main:refs/heads/$BR/main" \
    "refs/heads/$BR/work/*:refs/heads/$BR/work/*" \
    < /dev/null > "$tmp/p10setup" 2>&1
dd if=/dev/zero of="$tmp/dir/team/tiny1.bin" bs=1048576 count=1 2>/dev/null
git -C "$tmp/dir/team" add tiny1.bin
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team: add tiny1.bin (1 MB, after the big blobs are remote)"
hfetch ""
"$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/p10" 2> "$tmp/p10.err"
rc10=$?
if [ "$rc10" -ne 0 ]; then
    t_fail "11: P4d perturbation the 1 MB addition (with the big blobs already in the remote, the tracking refs stale) did not push clean (exit $rc10, want 0) -- the exclusion is ls-remote of the stored URL, not the mirror's tracking refs (which never move on a push by URL): $(tr '\n' ' ' < "$tmp/p10.err" | head_bytes 200)"
else
    t_ok "11: P4d perturbation the big blobs already in the remote (the stand-in's tips past the mirror's stale tracking refs) are NOT re-counted: only the ls-remote exclusion sees the remote already holds them, so adding one 1 MB blob pushes clean (exit 0, landed)"
fi


# --- 12: P4e -- zero pushed tips is FAIL-CLOSED, not a pass (F5) ------------
# The review's F5 (the note's behaviour 14): a scan that CANNOT run is exit
# 1, never exit 0. The port's key scan and size check resolved ZERO pushed
# tips ($BR/main + $BR/work/* absent from the mirror) by PRINTING a success
# line and returning 0 -- the gate reported success and the push child ran
# (to fail on the refspec). Construction: a FRESH rehearsal (dirE, init ONLY
# -- NO `mirror fetch`): init pushes only $BR/plan into the stand-in, and
# init's own initial fetch writes tracking refs (refs/remotes/origin/*) from
# the stand-in, never local refs/heads -- so the mirror holds NO $BR/main
# and NO $BR/work/* tip (the same gap cell 26's seed names: the team's
# $BR/main never reaches the stand-in until someone pushes it).
# `mirror push` must be REFUSED exit 1 by the gate's own fail-closed line
# (the key scan's: the port runs it before the size check), NOT exit 0, and
# the stand-in is UNCHANGED. A port that lets a zero-tip scan pass is red
# here (the prove-the-instrument red: the old `return 0` made this cell
# exit 0).
mkrehearsal "E" || fixture_fail "init (dirE) failed: $(tr '\n' ' ' < "$tmp/iE" | head_bytes 160)"
# no hfetch "E": the mirror's refs/heads must hold NO $BR/main / $BR/work tip.
git -C "$tmp/dirE/mirror.git" for-each-ref "refs/heads/$BR/main" "refs/heads/$BR/work" \
    --format='%(refname)' > "$tmp/p12tips" 2>&1
_standE_before=$(git -C "$tmp/dirE/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
"$HBIN" mirror push --config "$tmp/cfgE.json" "$tmp/dirE/mirror.git" \
    < /dev/null > "$tmp/p12" 2> "$tmp/p12.err"
rc12e=$?
if [ -s "$tmp/p12tips" ]; then
    t_fail "12: P4e the fixture is wrong: the mirror holds $BR tips ($BR/main and $BR/work/*) -- the zero-tip cell needs a mirror without them"
elif [ "$rc12e" -ne 1 ]; then
    t_fail "12: P4e a mirror that holds NO pushed tip ($BR/main, $BR/work/* absent) was NOT refused fail-closed (exit $rc12e, want 1): a scan that cannot run is never exit 0 -- $(tr '\n' ' ' < "$tmp/p12.err" | head_bytes 160)"
elif ! grep -qiE "the push is refused \(fail-closed" "$tmp/p12" "$tmp/p12.err"; then
    t_fail "12: P4e the gate did not emit its own zero-tip FAIL-CLOSED REFUSAL (it let the scan pass -- the latent shape where key_scan/size_check say() a '0 tips' success line and return 0, so only the push's refspec failure is the exit-1; the gate reported success): $(tr '\n' ' ' < "$tmp/p12" | head_bytes 160) / $(tr '\n' ' ' < "$tmp/p12.err" | head_bytes 160)"
elif [ "$_standE_before" != "$(git -C "$tmp/dirE/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)" ]; then
    t_fail "12: P4e the zero-tip refusal CHANGED the stand-in's $BR/main -- the push must not happen"
else
    t_ok "12: P4e a mirror that holds NO pushed tip ($BR/main, $BR/work/* absent) is REFUSED exit 1 by the gate's own fail-closed line (no pushed tip resolved in the mirror) -- a scan that cannot run is never exit 0 (behaviour 14), and the stand-in is UNCHANGED"
fi
# --- 13: P8 -- the push's GLOBAL-config isolation, behavioural --------------
# dir8's push (cell 8 landed it, so this repeat push adds nothing: the range
# is empty, the gates are silent, and the push child still runs -- that is
# what the env capture needs). A SUBSHELL that unsets the driver's own
# GIT_CONFIG_* exports and sets HOME=<fake home> whose .gitconfig
# plants a credential.helper = an executable that touches a marker (and
# answers a dummy credential). Assertions: (a) the push child's env dump
# shows the harness's OWN empty GIT_CONFIG_GLOBAL file -- exists, 0 bytes,
# under the harness's dir (NOT merely `GIT_CONFIG_GLOBAL=`; the driver's
# export is unset for this run, so only the harness's value can be there);
# (b) the marker is ABSENT (a local-path push consults no helper anyway --
# measured on git 2.53, in the harness AND in plain git; the guard is for the
# HTTPS form, whose end-to-end fire is the drive's own loop, def: the note's
# 'HTTPS end-to-end' deferred); (c) the push exits 0 and the stand-in's refs
# still equal the mirror's (the landing is idempotent).
_fakehome="$tmp/fakehome"; mkdir -p "$_fakehome"
_marker="$tmp/poison-marker"; rm -f "$_marker"
printf '#!/bin/sh\ntouch %s\nprintf "username=x\\npassword=y\\n\\n"\n' "$_marker" > "$_fakehome/helper"
chmod 755 "$_fakehome/helper"
printf '[credential]\n\thelper = %s\n' "$_fakehome/helper" > "$_fakehome/.gitconfig"
mkwrapper "$tmp/d11"
( unset GIT_CONFIG_GLOBAL GIT_CONFIG_SYSTEM; PATH="$tmp/d11:$PATH" \
    HOME="$_fakehome" "$HBIN" mirror push --config "$tmp/cfg8.json" "$tmp/dir8/mirror.git" \
    < /dev/null > "$tmp/p11" 2> "$tmp/p11.err" ); rc11=$?
_gv=""; _gv_size=""
if [ -e "$tmp/d11/dump" ]; then
    _gv=$(grep '^GIT_CONFIG_GLOBAL=' "$tmp/d11/dump" | tail -1 | cut -d= -f2-)
    if [ -n "$_gv" ] && [ -f "$_gv" ]; then _gv_size=$(wc -c < "$_gv" | tr -d ' '); fi
fi
_gv_under=0
case "$_gv" in "$tmp/dir8"/*) _gv_under=1 ;; esac
if [ "$rc11" -ne 0 ]; then
    t_fail "13: P8 the isolated push (subshell, driver GIT_CONFIG_* unset, fake HOME) exited $rc11 (want 0): $(tr '\n' ' ' < "$tmp/p11.err" | head_bytes 200)"
elif [ -z "$_gv" ] || [ -z "$_gv_size" ] || [ "$_gv_size" -ne 0 ]; then
    t_fail "13: P8 the push child's env did not carry the harness's OWN empty GIT_CONFIG_GLOBAL file (value='$_gv', size=$_gv_size) -- it must exist and be 0 bytes; the driver's export is unset for this run, so only the harness's value can be there"
elif [ "$_gv_under" -ne 1 ]; then
    t_fail "13: P8 the harness's GIT_CONFIG_GLOBAL file ($_gv) is NOT under the harness's dir ($tmp/dir8)"
elif [ -e "$_marker" ]; then
    t_fail "13: P8 the poisoned GLOBAL credential.helper FIRED (marker present) -- the push child saw the agent's global config (the isolation failed)"
else
    t_ok "13: P8 the isolated push exits 0; its env carries the harness's OWN 0-byte GIT_CONFIG_GLOBAL under the harness's dir (the driver's export unset for this run), and the marker is ABSENT (a local-path push consults no helper; the HTTPS fire is the drive's loop)"
fi

# (14) the control (the INSTRUMENT runs): a plain `git -C <mirror> push` from
# the SAME subshell env (no harness, no -c flags). For a local-path push the
# marker is ALSO absent (local never consults helpers -- the honest shape), so
# the control proves the wrapper dumps, not that the helper fires; cell 11's
# env-dump assertion is the tooth. This control passes on the red tree too
# (plain git needs no harness push) -- it validates the instrument, not the
# feature, so its being green on red is correct, not a hole.
rm -f "$_marker"
mkwrapper "$tmp/d12"
( unset GIT_CONFIG_GLOBAL GIT_CONFIG_SYSTEM; PATH="$tmp/d12:$PATH" \
    HOME="$_fakehome" git -C "$tmp/dir8/mirror.git" push -q "$tmp/dir8/standin.git" \
    "refs/heads/$BR/main:refs/heads/$BR/main" \
    "refs/heads/$BR/work/*:refs/heads/$BR/work/*" \
    < /dev/null > "$tmp/p12" 2> "$tmp/p12.err" ); rc12=$?
if [ "$rc12" -ne 0 ]; then
    t_fail "14: P8 control the plain git push (subshell, no harness) exited $rc12 (want 0): $(tr '\n' ' ' < "$tmp/p12.err" | head_bytes 160)"
elif [ ! -e "$tmp/d12/dump" ]; then
    t_fail "14: P8 control the plain-git dump is ABSENT -- the PATH wrapper (the instrument) never ran, so cell 11's absence claim is unproven"
elif [ -e "$_marker" ]; then
    t_fail "14: P8 control the plain local-path push FIRED the poisoned GLOBAL helper -- measured on git 2.53 a local-path push consults NO helper, so the fixture is wrong"
else
    t_ok "14: P8 control a plain \`git -C <mirror> push\` from the same subshell env runs the instrument (the dump EXISTS) and the marker is ABSENT (a local-path push consults no helper -- the instrument is what cell 11's env-dump assertion relies on)"
fi

# --- 15: P10 -- the push child's argv: the full form -------------------------
# dir8's clean push (cells 8/11 landed it, so the range is empty; the wrapper
# re-captures the push child's argv). The argv must show, in order: the M2 -c
# flags, -c core.hooksPath=/dev/null, -c credential.helper= (the reset), then
# -c credential.helper=<ABS-JICHI-HARNESS> credential-helper (the two-word
# spec), push -q <stand-in absolute path> (NOT `origin`), the two refspecs,
# NO --force, NO refs/kept/* refspec.
rm -rf "$tmp/d13"
mkwrapper "$tmp/d13"
PATH="$tmp/d13:$PATH" "$HBIN" mirror push --config "$tmp/cfg8.json" "$tmp/dir8/mirror.git" \
    < /dev/null > "$tmp/p13" 2> "$tmp/p13.err"
rc13=$?
_ln13=$(pushargv "$tmp/d13")
if [ "$rc13" -ne 0 ]; then
    t_fail "15: P10 the push (for the argv capture) exited $rc13 (want 0): $(tr '\n' ' ' < "$tmp/p13.err" | head_bytes 200)"
elif [ -z "$_ln13" ]; then
    t_fail "15: P10 the git wrapper captured NO push argv -- the instrument never ran"
else
    _pos_hooks=$(printf '%s\n' "$_ln13" | awk '{ for (i = 1; i <= NF; i++) if ($i == "core.hooksPath=/dev/null") { print i; exit } }')
    _pos_reset=$(printf '%s\n' "$_ln13" | awk '{ for (i = 1; i <= NF; i++) if ($i == "credential.helper=") { print i; exit } }')
    _pos_spec=$(printf '%s\n' "$_ln13" | awk '{ for (i = 1; i <= NF; i++) if ($i == "credential-helper") { print i; exit } }')
    _pos_push=$(printf '%s\n' "$_ln13" | awk '{ for (i = 1; i <= NF; i++) if ($i == "push") { print i; exit } }')
    _pos_fsmonitor=$(printf '%s\n' "$_ln13" | awk '{ for (i = 1; i <= NF; i++) if ($i == "core.fsmonitor=") { print i; exit } }')
    if [ -z "$_pos_hooks" ] || [ -z "$_pos_reset" ] || [ -z "$_pos_spec" ] || [ -z "$_pos_push" ]; then
        t_fail "15: P10 the push child's argv is missing a required element (hooksPath=$(_pos_hooks:-none) reset=$(_pos_reset:-none) two-word-spec=$(_pos_spec:-none) push=$(_pos_push:-none)): $_ln13"
    elif [ "$_pos_hooks" -lt "$_pos_fsmonitor" ] || [ "$_pos_reset" -lt "$_pos_hooks" ] || [ "$_pos_spec" -lt "$_pos_reset" ] || [ "$_pos_push" -lt "$_pos_spec" ]; then
        t_fail "15: P10 the push child's argv is OUT OF ORDER (M2 fsmonitor=$(_pos_fsmonitor:-none) < hooksPath=$_pos_hooks < reset=$_pos_reset < two-word-spec=$_pos_spec < push=$_pos_push): $_ln13"
    elif ! printf '%s\n' "$_ln13" | grep -qF "$tmp/dir8/standin.git"; then
        t_fail "15: P10 the push child's argv does not carry the stand-in's ABSOLUTE path ($tmp/dir8/standin.git): $_ln13"
    elif printf '%s\n' "$_ln13" | grep -qw origin; then
        t_fail "15: P10 the push child's argv carries the bare \`origin\` name (behaviour 2: never the origin name): $_ln13"
    elif printf '%s\n' "$_ln13" | grep -q -- --force; then
        t_fail "15: P10 the push child's argv carries --force (never allowed, behaviour 12): $_ln13"
    elif printf '%s\n' "$_ln13" | grep -qF "refs/kept/"; then
        t_fail "15: P10 the push child's argv carries a refs/kept/* refspec (the keep-gate default is OFF, behaviour 1): $_ln13"
    elif ! printf '%s\n' "$_ln13" | grep -qF "refs/heads/$BR/main" || ! printf '%s\n' "$_ln13" | grep -qF "refs/heads/$BR/work"; then
        t_fail "15: P10 the push child's argv lacks the two refspecs ($BR/main, $BR/work/*): $_ln13"
    else
        t_ok "15: P10 the push child's argv shows, in order: the M2 -c flags, -c core.hooksPath=/dev/null, -c credential.helper= (the reset), -c credential.helper=<ABS-JICHI-HARNESS> credential-helper (the two-word spec), push -q <absolute path> (not origin), the two refspecs, NO --force, NO refs/kept/* (behaviours 9, 10, 2, 12, 15)"
    fi
fi

# --- 16: P2b -- the global insteadOf is NEUTRALISED, not detected ------------
# A poisoned GLOBAL gitconfig (via a subshell GIT_CONFIG_GLOBAL for the PUSH
# run only, the driver's export superseded by it) with an insteadOf that
# redirects the stored URL to $tmp/other.git. The guard's get-url child AND
# the push child BOTH carry the harness's empty GIT_CONFIG_GLOBAL (behaviour
# 8's isolation), so the redirect is invisible to both (01:37 item 3: the
# earlier "the get-url child reads the global config" claim was wrong). So:
# `mirror push` exits 0 and the STAND-IN's $BR/main ADVANCES to the mirror's
# tip (the push landed at the stored URL), while $tmp/other.git -- the
# redirect target -- STAYS EMPTY (for-each-ref shows nothing). The wrapper
# dump must show the push child ran with the stored absolute URL, NOT the
# redirect target. A port that pushed the redirected URL is red (other.git
# gains the refs). The URL guard's DETECTION teeth are the mirror-local plants
# (cell 2), not this cell; P8's marker cell is the general evidence for the
# isolation this cell rides on. (A fresh commit on dir's team + hfetch first,
# so the mirror's tip genuinely advances -- cell 10 already landed everything
# earlier, so the stand-in's main equals the mirror's until this commit.)
_poison_gitcfg="$tmp/poison-gitconfig"
printf '[url "file://%s"]\n\tinsteadOf = %s\n' "$tmp/other.git" "$stored_url" > "$_poison_gitcfg"
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: a fresh $BR/main commit for the P2b landing check"
hfetch ""
_s_before14=$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
_m_before14=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
mkwrapper "$tmp/d14"
PATH="$tmp/d14:$PATH" GIT_CONFIG_GLOBAL="$_poison_gitcfg" GIT_CONFIG_SYSTEM=/dev/null \
    "$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/p14" 2> "$tmp/p14.err"
rc14=$?
_ln14=$(pushargv "$tmp/d14")
if [ "$rc14" -ne 0 ]; then
    t_fail "16: P2b the neutralised push (poisoned GLOBAL insteadOf, isolated run) exited $rc14 (want 0) -- the redirect is invisible to BOTH the guard's get-url child and the push child (the harness's empty GIT_CONFIG_GLOBAL), so it must land at the stored URL: $(tr '\n' ' ' < "$tmp/p14.err" | head_bytes 200)"
elif [ -n "$(git -C "$tmp/other.git" for-each-ref 2>/dev/null)" ]; then
    t_fail "16: P2b the redirected repo ($tmp/other.git) is NOT EMPTY -- the push child followed the poisoned global insteadOf (the isolation failed): $(git -C "$tmp/other.git" for-each-ref | head_bytes 120)"
elif [ -z "$_ln14" ]; then
    t_fail "16: P2b the push exited 0 but the git wrapper captured NO push argv -- the instrument never ran"
elif ! printf '%s\n' "$_ln14" | grep -qF "$tmp/dir/standin.git"; then
    t_fail "16: P2b the push child's argv does not carry the stored absolute URL ($tmp/dir/standin.git): $_ln14"
elif [ "$_s_before14" = "$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)" ] \
    || [ "$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)" != "$_m_before14" ]; then
    t_fail "16: P2b the stand-in's $BR/main did not ADVANCE to the mirror's tip (before=$_s_before14 mirror=$_m_before14) -- the push must land at the stored URL"
else
    t_ok "16: P2b a poisoned GLOBAL insteadOf redirecting the stored URL is NEUTRALISED, not detected: the push exits 0 and lands at the stored URL (stand-in's $BR/main advanced to the mirror's tip), $tmp/other.git stays EMPTY, and the push child's argv carries the stored absolute URL -- a port that pushed the redirected URL is red"
fi

# --- 17: P11 -- the credential helper get (direct, no push) ------------------
# The helper parses git's credential-input on STDIN (01:37 item 2). For the
# LOCAL rehearsal the stored URL is a plain path, so the identity that must
# ANSWER is protocol=file + an EMPTY host (file URLs have no host) -- fed as
# `protocol=file\nhost=\n\n`. With JICHI_HARNESS_GIT_TOKEN_FILE=<token file>,
# JICHI_HARNESS_CRED_RULE=oauth2 and JICHI_HARNESS_CRED_URL=<the stored URL>
# in the env, `credential-helper get` prints exactly `username=oauth2` then
# `password=<canary>`. The contract: on `get` the helper requires the stdin
# identity to match the configured remote URL (for a file-URL: protocol=file
# and host empty); any other identity is refused (exit 1, NO password= line,
# the refusal on stderr). That match is the race-free layer against a
# check-then-use race in the URL guard: a same-user process can change the
# mirror config between the guard's check and the push, but the helper's
# refusal of a remote it is not configured for cannot be raced. On the red
# tree `credential-helper` is an unknown command -> exit 2.
exp15="$tmp/exp15"; printf 'username=oauth2\npassword=%s\n' "$TOKEN_CANARY" > "$exp15"
printf 'protocol=file\nhost=\n\n' | JICHI_HARNESS_GIT_TOKEN_FILE="$tmp/tokfile" \
    JICHI_HARNESS_CRED_RULE=oauth2 JICHI_HARNESS_CRED_URL="$stored_url" \
    "$HBIN" credential-helper get > "$tmp/p15" 2> "$tmp/p15.err"
rc15=$?
if [ "$rc15" -ne 0 ]; then
    t_fail "17: P11 \`credential-helper get\` (stdin identity protocol=file + empty host) exited $rc15 (want 0) -- on the unfixed tree it is an unknown command: $(tr '\n' ' ' < "$tmp/p15.err" | head_bytes 200)"
elif ! diff -q "$exp15" "$tmp/p15" >/dev/null 2>&1; then
    t_fail "17: P11 \`credential-helper get\` did not print exactly username=oauth2 then password=<token> (output: $(tr '\n' ' ' < "$tmp/p15" | head_bytes 120))"
else
    t_ok "17: P11 \`credential-helper get\` fed the file-URL identity on stdin (protocol=file, host empty) with the token file, CRED_RULE=oauth2 and CRED_URL=<stored URL> in the env prints exactly username=oauth2 then password=<canary>"
fi


# --- 18: P11g -- the PATH GUARD (F2: the path half of the identity) --------
# The review's F2: the helper parsed `path` from stdin and then (void)'d it
# -- with -c credential.useHttpPath=true, a request for a DIFFERENT project
# on the SAME host was answered with the token: the exact same-uid redirect
# the identity guard exists to hold. Construction: CRED_URL at a SCHEME url
# (https://git.example.org/drive/remote.git), the token file at the canary,
# the rule oauth2, and a stdin identity whose protocol AND host MATCH but
# whose path is other/project.git (a different project on the same host, in
# git's form -- no leading slash, as useHttpPath=true sends it)
# -> REFUSED exit 1, NO password= line on stdout, and the refusal on stderr
# names the remote it will not serve AND the requested path. (git.example.org
# is the drive's rehearsal stand-in for a real HTTPS remote: never a real
# GitLab, the secret rule.)
printf 'protocol=https\nhost=git.example.org\npath=other/project.git\n\n' \
    | JICHI_HARNESS_GIT_TOKEN_FILE="$tmp/tokfile" JICHI_HARNESS_CRED_RULE=oauth2 \
      JICHI_HARNESS_CRED_URL="https://git.example.org/drive/remote.git" \
      "$HBIN" credential-helper get > "$tmp/p18g" 2> "$tmp/p18g.err"
rc18g=$?
if [ "$rc18g" -ne 1 ]; then
    t_fail "18: P11g the path guard (a SCHEME url, the protocol + host MATCH, the path is a DIFFERENT project other/project.git, git's form) exited $rc18g (want 1) -- a request for a different project on the same host must not be answered with the token: $(tr '\n' ' ' < "$tmp/p18g.err" | head_bytes 160)"
elif grep -q "password=" "$tmp/p18g"; then
    t_fail "18: P11g the path guard refused a different path but stdout carries a password= line -- the token went to a remote it is not configured for: $(tr '\n' ' ' < "$tmp/p18g" | head_bytes 120)"
elif ! grep -qF "git.example.org/drive/remote.git" "$tmp/p18g.err"; then
    t_fail "18: P11g the path guard's refusal does not NAME the configured remote on stderr: $(tr '\n' ' ' < "$tmp/p18g.err" | head_bytes 200)"
elif ! grep -q "path: other/project.git)" "$tmp/p18g.err"; then
    t_fail "18: P11g the path guard's refusal does not NAME the requested path (other/project.git, git's form) on stderr: $(tr '\n' ' ' < "$tmp/p18g.err" | head_bytes 200)"
else
    t_ok "18: P11g the path guard: with a SCHEME CRED_URL and the protocol + host matching, a stdin identity whose path is a DIFFERENT project (other/project.git, git's form) is refused exit 1, stdout carries NO password= line, and the refusal names the configured remote AND the requested path -- the path is compared, not voided (the same-uid redirect the identity guard holds)"
fi

# --- 19: P11h -- the MATCHING scheme cell (the path guard's happy half) -----
# The same SCHEME CRED_URL, the same token file, the same rule, and a stdin
# identity whose protocol, host AND path ALL match the configured remote
# (path=drive/remote.git, the url's path part byte-exact in GIT'S form --
# no leading slash, as useHttpPath=true sends it) -> ANSWERED exit
# 0 with exactly username=oauth2 then password=<canary>. This is the control
# for cell 18: it proves the token IS served when the full identity (the
# path included) matches, so cell 18's refusal is the path comparison, not
# an over-broad guard. Its stdout is the one MORE designed exception in the
# cell 29 canary sweep (the credential is the helper's output channel, by
# design).
exp19h="$tmp/exp19h"; printf 'username=oauth2\npassword=%s\n' "$TOKEN_CANARY" > "$exp19h"
printf 'protocol=https\nhost=git.example.org\npath=drive/remote.git\n\n' \
    | JICHI_HARNESS_GIT_TOKEN_FILE="$tmp/tokfile" JICHI_HARNESS_CRED_RULE=oauth2 \
      JICHI_HARNESS_CRED_URL="https://git.example.org/drive/remote.git" \
      "$HBIN" credential-helper get > "$tmp/p19h" 2> "$tmp/p19h.err"
rc19h=$?
if [ "$rc19h" -ne 0 ]; then
    t_fail "19: P11h the MATCHING scheme identity (protocol=https, host=git.example.org, path=drive/remote.git == CRED_URL's path, git's form) exited $rc19h (want 0) -- the guard is over-broad: $(tr '\n' ' ' < "$tmp/p19h.err" | head_bytes 160)"
elif ! diff -q "$exp19h" "$tmp/p19h" >/dev/null 2>&1; then
    t_fail "19: P11h the matching identity did not print exactly username=oauth2 then password=<token> (output: $(tr '\n' ' ' < "$tmp/p19h" | head_bytes 120))"
else
    t_ok "19: P11h the MATCHING scheme identity (protocol, host AND path all equal to CRED_URL's) is ANSWERED exit 0 with exactly username=oauth2 then password=<canary> -- the path is compared, not voided, and the guard answers when it should (cell 18's control)"
fi
# --- 20: P11b -- credential-helper store + erase print nothing, exit 0 -------
printf 'protocol=file\nhost=\n\n' | JICHI_HARNESS_GIT_TOKEN_FILE="$tmp/tokfile" \
    JICHI_HARNESS_CRED_RULE=oauth2 JICHI_HARNESS_CRED_URL="$stored_url" \
    "$HBIN" credential-helper store > "$tmp/p16s" 2> "$tmp/p16s.err"
rc16s=$?
printf 'protocol=file\nhost=\n\n' | JICHI_HARNESS_GIT_TOKEN_FILE="$tmp/tokfile" \
    JICHI_HARNESS_CRED_RULE=oauth2 JICHI_HARNESS_CRED_URL="$stored_url" \
    "$HBIN" credential-helper erase > "$tmp/p16e" 2> "$tmp/p16e.err"
rc16e=$?
if [ "$rc16s" -ne 0 ] || [ "$rc16e" -ne 0 ]; then
    t_fail "20: P11b \`credential-helper store\` exit $rc16s / \`erase\` exit $rc16e (both want 0) -- on the unfixed tree they are an unknown command"
elif [ -s "$tmp/p16s" ] || [ -s "$tmp/p16e" ]; then
    t_fail "20: P11b \`credential-helper store\`/`erase` printed output (git must never be able to persist the token) -- store: $(tr '\n' ' ' < "$tmp/p16s" | head_bytes 80)"
else
    t_ok "20: P11b \`credential-helper store\` and \`erase\` print NOTHING and exit 0 (git can never persist the token)"
fi

# --- 21: P11c -- a MISSING token file is exit 1 (refusal named) --------------
printf 'protocol=file\nhost=\n\n' | JICHI_HARNESS_GIT_TOKEN_FILE="$tmp/no-such-token-file" \
    JICHI_HARNESS_CRED_RULE=oauth2 JICHI_HARNESS_CRED_URL="$stored_url" \
    "$HBIN" credential-helper get > "$tmp/p17" 2> "$tmp/p17.err"
rc17=$?
if [ "$rc17" -ne 1 ]; then
    t_fail "21: P11c \`credential-helper get\` with a MISSING token file exited $rc17 (want 1) -- on the unfixed tree it is an unknown command: $(tr '\n' ' ' < "$tmp/p17.err" | head_bytes 160)"
elif ! grep -qiE "token|JICHI_HARNESS_GIT_TOKEN_FILE|file" "$tmp/p17" "$tmp/p17.err"; then
    t_fail "21: P11c the missing-token-file refusal does not name it: $(tr '\n' ' ' < "$tmp/p17.err" | head_bytes 200)"
else
    t_ok "21: P11c \`credential-helper get\` with a missing token file is exit 1 (the refusal names the missing file)"
fi

# --- 22: P11d -- a token file shorter than 12 bytes is exit 1 ----------------
printf 'short\n' > "$tmp/short-tok"; chmod 600 "$tmp/short-tok"
printf 'protocol=file\nhost=\n\n' | JICHI_HARNESS_GIT_TOKEN_FILE="$tmp/short-tok" \
    JICHI_HARNESS_CRED_RULE=oauth2 JICHI_HARNESS_CRED_URL="$stored_url" \
    "$HBIN" credential-helper get > "$tmp/p18" 2> "$tmp/p18.err"
rc18=$?
if [ "$rc18" -ne 1 ]; then
    t_fail "22: P11d \`credential-helper get\` with a <12-byte token file exited $rc18 (want 1) -- on the unfixed tree it is an unknown command: $(tr '\n' ' ' < "$tmp/p18.err" | head_bytes 160)"
elif ! grep -qiE "token|could not be loaded|short" "$tmp/p18" "$tmp/p18.err"; then
    t_fail "22: P11d the short-token refusal does not name it: $(tr '\n' ' ' < "$tmp/p18.err" | head_bytes 200)"
else
    t_ok "22: P11d \`credential-helper get\` with a token file shorter than 12 bytes is exit 1 (the token could not be loaded)"
fi

# --- 23: P11e -- the canary is NOT an argument to the helper (argv shim) -----
# Wrap jichi-harness in a tiny argv-dump shim and assert the canary is NOT an
# argument (it is in the env value + output only -- the reference's channel).
# The shim dumps to a FILE ($tmp/d19), not a PATH dir -- it wraps the harness
# directly, so no PATH interception is needed.
_shim="$tmp/harness-shim"; mkdir -p "$_shim"
_d19="$tmp/d19"; rm -f "$_d19"
printf '#!/bin/sh\n{ printf "ARGV:\\n"; for _a in "$@"; do printf "%%s\\n" "$_a"; done; printf "ENDARGV:\\n"; } >> %s\nexec %s "$@"\n' "$_d19" "$HBIN" > "$_shim/harness"
chmod 755 "$_shim/harness"
printf 'protocol=file\nhost=\n\n' | JICHI_HARNESS_GIT_TOKEN_FILE="$tmp/tokfile" \
    JICHI_HARNESS_CRED_RULE=oauth2 JICHI_HARNESS_CRED_URL="$stored_url" \
    "$_shim/harness" credential-helper get > "$tmp/p19" 2> "$tmp/p19.err"
rc19=$?
if [ ! -e "$_d19" ]; then
    t_fail "23: P11e the argv-dump shim captured NOTHING -- the instrument never ran"
elif grep -qF "$TOKEN_CANARY" "$_d19"; then
    t_fail "23: P11e the token canary IS an argument to the helper (it must be in the env value + output only, never on the command line)"
elif [ "$rc19" -ne 0 ]; then
    t_fail "23: P11e \`credential-helper get\` via the argv shim exited $rc19 (want 0): $(tr '\n' ' ' < "$tmp/p19.err" | head_bytes 160)"
else
    t_ok "23: P11e the token canary is NOT an argument to the helper (the argv-dump shim shows it is carried only by the env value + the output)"
fi

# --- 24: P11f -- the HOST GUARD (01:37 item 2, the new cell) -----------------
# The helper's stdin-identity match, its refusal side: with the token file,
# CRED_RULE=oauth2 and CRED_URL=<the stored URL> in the env, feed it a
# DIFFERENT host on stdin (`protocol=file\nhost=another\npath=/x\n\n`) -> the
# identity does not match the configured remote URL, so `get` is REFUSED:
# exit 1, NO `password=` line on stdout, and the refusal on stderr NAMES the
# remote it will not serve (the stored URL). This is the race-free layer
# against a check-then-use race in the URL guard: a same-user process can
# change the mirror config between the guard's check and the push (the
# effective URL git dials is what it re-reads at push time), but the helper
# cannot be made to answer for a remote it is not configured for -- its
# refusal is inside the token channel itself. On the red tree the helper is
# an unknown command -> exit 2, the standard red.
printf 'protocol=file\nhost=another\npath=/x\n\n' \
    | JICHI_HARNESS_GIT_TOKEN_FILE="$tmp/tokfile" JICHI_HARNESS_CRED_RULE=oauth2 \
      JICHI_HARNESS_CRED_URL="$stored_url" \
      "$HBIN" credential-helper get > "$tmp/p20hg" 2> "$tmp/p20hg.err"
rc20hg=$?
if [ "$rc20hg" -ne 1 ]; then
    t_fail "24: P11f the host guard (a DIFFERENT host on stdin) exited $rc20hg (want 1) -- on the unfixed tree the helper is an unknown command: $(tr '\n' ' ' < "$tmp/p20hg.err" | head_bytes 160)"
elif grep -q "password=" "$tmp/p20hg"; then
    t_fail "24: P11f the host guard refused a different host but stdout carries a password= line -- the token went to a remote it is not configured for: $(tr '\n' ' ' < "$tmp/p20hg" | head_bytes 120)"
elif ! grep -qF "$stored_url" "$tmp/p20hg.err"; then
    t_fail "24: P11f the host guard's refusal does not NAME the remote it will not serve ($stored_url) on stderr: $(tr '\n' ' ' < "$tmp/p20hg.err" | head_bytes 200)"
else
    t_ok "24: P11f the host guard: credential-helper get fed a DIFFERENT host on stdin is refused exit 1, stdout carries NO password= line, and the refusal on stderr names the remote it will not serve (the stored URL) -- the race-free layer against a check-then-use race in the URL guard"
fi

# --- 25: P11f2 the HOST guard ISOLATED (same protocol + path, DIFFERENT host)
# SV:1126 (the supervisor's teeth on 1879e41a): the existing host-guard cell
# (24, P11f) stays GREEN when the host comparison at cred.c:313 is disabled
# (`if (0 && strcmp(host_in, exp_host) != 0)`) -- P11f's stdin identity differs
# in host AND path (protocol=file, host=another, path=/x against a bare-path
# CRED_URL): the guard order is proto (307), host (313), path (319), so in the
# INTACT tree it is refused at the host line, but with the host line disabled
# the PATH line (319, /x vs the url's empty path) still refuses it, so the
# cell stays green either way. This cell isolates the host redirect: a
# SCHEME CRED_URL (https://git.example.org/drive/remote.git -- the drive's
# rehearsal stand-in for a real HTTPS remote, never a real GitLab, the secret
# rule), the token file at the canary, the rule oauth2, and a stdin identity
# whose protocol AND path MATCH (protocol=https, path=drive/remote.git,
# byte-exact to CRED_URL's path in GIT'S form -- no leading slash, as
# useHttpPath=true sends it) and whose ONLY difference is the host
# (host=evil.example). Guard intact -> refused exit 1, stdout carries NO
# password= line, the refusal on stderr says the identity does not match and
# names the configured remote. With cred.c:313 disabled the identity passes
# proto + path and the token IS served (exit 0, a password= line) -- the cell
# goes red: the refusal can only be the HOST comparison.
printf 'protocol=https\nhost=evil.example\npath=drive/remote.git\n\n' \
    | JICHI_HARNESS_GIT_TOKEN_FILE="$tmp/tokfile" JICHI_HARNESS_CRED_RULE=oauth2 \
      JICHI_HARNESS_CRED_URL="https://git.example.org/drive/remote.git" \
      "$HBIN" credential-helper get > "$tmp/p20h" 2> "$tmp/p20h.err"
rc20h=$?
if [ "$rc20h" -ne 1 ]; then
    t_fail "25: P11f2 the ISOLATED host guard (the protocol AND path MATCH, ONLY the host differs, evil.example) exited $rc20h (want 1) -- with the host comparison (cred.c:313) disabled the token would be served (exit 0): $(tr '\n' ' ' < "$tmp/p20h.err" | head_bytes 160)"
elif grep -q "password=" "$tmp/p20h"; then
    t_fail "25: P11f2 the ISOLATED host guard refused a different host but stdout carries a password= line -- the token went to a remote it is not configured for: $(tr '\n' ' ' < "$tmp/p20h" | head_bytes 120)"
elif ! grep -qF "git.example.org/drive/remote.git" "$tmp/p20h.err"; then
    t_fail "25: P11f2 the ISOLATED host guard's refusal does not NAME the configured remote (git.example.org/drive/remote.git) on stderr: $(tr '\n' ' ' < "$tmp/p20h.err" | head_bytes 200)"
elif ! grep -qiE "identity does not match" "$tmp/p20h.err"; then
    t_fail "25: P11f2 the ISOLATED host guard's refusal does not say the identity does not match on stderr: $(tr '\n' ' ' < "$tmp/p20h.err" | head_bytes 200)"
else
    t_ok "25: P11f2 the HOST guard ISOLATED: with a SCHEME CRED_URL the protocol AND path MATCH and ONLY the host differs (evil.example vs git.example.org), credential-helper get is refused exit 1, stdout carries NO password= line, and the refusal names the configured remote -- the refusal is the HOST comparison (cred.c:313), not the path guard, the isolated tooth the existing host-guard cell (P11f) lacked"
fi

# --- 26: P12 -- the non-FF push is a FAILURE (unlike the fetch) --------------
# dir9 (clean history, so the key scan is silent): the stand-in's $BR/main
# gets a diverged commit pushed directly to it from a clone of the stand-in,
# while dir9's team gets its own $BR/main commit and the mirror fetches it --
# the two $BR/main tips share a parent and neither descends from the other.
# The push's $BR/main refspec is rejected non-FF: exit 1 with git's `(fetch
# first)` refusal captured, the stand-in UNCHANGED. A port that tolerates a
# rejected push (exit 0) is red.
# FIXTURE GAP (the code worker's finding, 89e7fe3f): init pushes only $BR/plan
# into the stand-in, so dir9's stand-in had NO $BR/main -- the --no-checkout
# clone's `checkout -b diverged origin/$BR/main` failed silently ('not a
# commit') and the push created the ref fresh (fast-forward, exit 0). The
# seed below pushes dir9's team $BR/main into the stand-in BEFORE the clone,
# so the diverged commit and team9's new commit share a parent (the seed
# commit) and the push's $BR/main refspec is genuinely non-FF.
_d20src="$tmp/d20src"
git -C "$tmp/dir9/team" push -q "$tmp/dir9/standin.git" \
    "refs/heads/$BR/main:refs/heads/$BR/main" > "$tmp/p20seed" 2>&1
_seed20_rc=$?
# LAUD GUARD (the 06:37-review finding: the seed used to be allowed to fail
# SILENTLY -- a failed seed push leaves $BR/main ABSENT in the stand-in, so the
# later --no-checkout clone's `checkout -b diverged origin/$BR/main` is a no-op
# and the cell's own push CREATES the ref fresh (fast-forward, exit 0) and the
# whole non-FF block fails silently. The runbook rule: read the extracted set,
# not its size -- if the seed did not land, fail LOUD with the seed's output
# quoted, instead of letting the cell mask its own precondition.)
_seed20_ref=$(git -C "$tmp/dir9/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
if [ "$_seed20_rc" -ne 0 ] || [ -z "$_seed20_ref" ]; then
    # FAIL LOUD (the 06:37-review finding): the seed did not land, so the
    # non-FF precondition is broken. Emit the single cell-25 result here and
    # SKIP the diverge+push block (which would otherwise create the ref fresh
    # and mask the precondition as a false "non-FF"). The runbook rule: read
    # the extracted set, not its size -- quote the seed's output.
    t_fail "26: P12 the SEED push did not land (rc=$_seed20_rc, $BR/main in the stand-in: '$_seed20_ref') -- the non-FF precondition is broken; seed output: $(tr '\n' ' ' < "$tmp/p20seed" | head_bytes 200)"
else
    # A --no-checkout clone of the stand-in has $BR/main at origin/$BR/main (a
    # local branch only if it were HEAD), so diverge from the origin ref.
    { git clone -q --no-checkout "$tmp/dir9/standin.git" "$_d20src" \
      && git -C "$_d20src" checkout -q -b "diverged" "origin/$BR/main" \
      && git -C "$_d20src" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
           commit -q --allow-empty -m "standin9: a diverged $BR/main commit" \
      && git -C "$_d20src" push -q "$tmp/dir9/standin.git" "diverged:refs/heads/$BR/main"; } \
      > "$tmp/p20setup" 2>&1
    git -C "$tmp/dir9/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
        commit -q --allow-empty -m "team9: a $BR/main commit the stand-in does not have"
    hfetch "9"
    _stand20_before=$(git -C "$tmp/dir9/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
    "$HBIN" mirror push --config "$tmp/cfg9.json" "$tmp/dir9/mirror.git" \
        < /dev/null > "$tmp/p20" 2> "$tmp/p20.err"
    rc20=$?
    _stand20_after=$(git -C "$tmp/dir9/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
    if [ "$rc20" -ne 1 ]; then
        t_fail "26: P12 a non-FF push (the stand-in's $BR/main diverged) exited $rc20 (want 1) -- a rejected push is a FAILURE, not a tolerated report: $(tr '\n' ' ' < "$tmp/p20.err" | head_bytes 160)"
    elif ! grep -qiE "fetch first|non-fast-forward|rejected" "$tmp/p20" "$tmp/p20.err"; then
        t_fail "26: P12 the non-FF push exited 1 but git's refusal is not captured (want '(fetch first)' / non-fast-forward): $(tr '\n' ' ' < "$tmp/p20" | head_bytes 120) / $(tr '\n' ' ' < "$tmp/p20.err" | head_bytes 120)"
    elif [ "$_stand20_before" != "$_stand20_after" ]; then
        t_fail "26: P12 the non-FF push CHANGED the stand-in's $BR/main ($_stand20_before -> $_stand20_after)"
    else
        t_ok "26: P12 a non-FF push (the stand-in's $BR/main diverged) exits 1 with git's (fetch first) refusal captured, the stand-in UNCHANGED -- a rejected push is a FAILURE, unlike the fetch's tolerated report line"
    fi
fi

# --- 27: P1ls -- the ls-remote child's argv + env (P1's tooth, 06:37 review) --
# Run a normal push on a CLEAN fixture (dir11) under the git wrapper (cell 13's
# idiom: a PATH shim that dumps each child's argv + env to DIR/dump and execs
# the real git). The KEY SCAN's exclusion runs `git ls-remote <stored-url>`
# before the push; find that child in the argv dump (the block carrying a
# standalone `ls-remote`) and assert it carries, IN ORDER: -c
# credential.helper= (the RESET), then -c
# credential.helper=<ABS-JICHI-HARNESS> credential-helper (the SET), then -c
# credential.useHttpPath=true; and that its env dump carries the THREE NAMES
# JICHI_HARNESS_GIT_TOKEN_FILE + JICHI_HARNESS_CRED_RULE + JICHI_HARNESS_CRED_URL.
# Pre-fix the ls-remote child carried only the RESET + useHttpPath + CRED_URL
# (no SET, no token-file, no rule) -- against GitLab over HTTPS it had no
# helper and the ls-remote failed, so the key scan refused fail-closed and
# every real push was refused. This cell is the instrument that catches that
# asymmetry. The push itself is a clean landing (exit 0), so the ls-remote
# child ran (the key scan passed before the push).
_d26="$tmp/d26"
mkwrapper "$_d26"
git -C "$tmp/dir11/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team11: a clean $BR/main commit"
hfetch "11"
PATH="$_d26:$PATH" "$HBIN" mirror push --config "$tmp/cfg11.json" "$tmp/dir11/mirror.git" \
    < /dev/null > "$tmp/p26" 2> "$tmp/p26.err"
rc26=$?
_ls26=$(lsargv "$_d26")
if [ "$rc26" -ne 0 ]; then
    t_fail "27: P1ls the clean push under the wrapper did not exit 0 (rc=$rc26) -- the ls-remote child only runs when the key scan passes: $(tr '\n' ' ' < "$tmp/p26.err" | head_bytes 200)"
elif [ -z "$_ls26" ]; then
    t_fail "27: P1ls no `ls-remote` child in the wrapper's argv dump -- the key scan's exclusion did not run: $(tr '\n' ' ' < "$tmp/d26/dump" | head_bytes 200)"
else
    # The -c flags, in ORDER: the RESET, the SET (the two-word helper spec),
    # useHttpPath. The joined argv carries them as `-c credential.helper=`,
    # `-c credential.helper=<ABS> credential-helper`, `-c
    # credential.useHttpPath=true`.
    _ls26_ok=1
    case " $_ls26 " in
        *" -c credential.helper= -c credential.helper="$HBIN" credential-helper -c credential.useHttpPath=true "*) : ;;
        *) _ls26_ok=0 ;;
    esac
    if [ "$_ls26_ok" -ne 1 ]; then
        t_fail "27: P1ls the ls-remote child's argv does NOT show, in order, the RESET (-c credential.helper=), the SET (-c credential.helper=<ABS-JICHI-HARNESS> credential-helper) and -c credential.useHttpPath=true (P1: the ls-remote must be built like the push child): $(printf '%s' "$_ls26" | head_bytes 300)"
    else
        _ls26_env=$(lsenv "$_d26")
        _ls26_envok=1
        printf '%s\n' "$_ls26_env" | grep -qF 'JICHI_HARNESS_GIT_TOKEN_FILE=' || _ls26_envok=0
        printf '%s\n' "$_ls26_env" | grep -qF 'JICHI_HARNESS_CRED_RULE=' || _ls26_envok=0
        printf '%s\n' "$_ls26_env" | grep -qF 'JICHI_HARNESS_CRED_URL=' || _ls26_envok=0
        if [ "$_ls26_envok" -ne 1 ]; then
            t_fail "27: P1ls the ls-remote child's env does NOT carry all THREE of JICHI_HARNESS_GIT_TOKEN_FILE + JICHI_HARNESS_CRED_RULE + JICHI_HARNESS_CRED_URL (P1: the full identity env, not just CRED_URL): $(printf '%s' "$_ls26_env" | grep -F 'JICHI_HARNESS' | head_bytes 200)"
        else
            t_ok "27: P1ls (the P1 fix's tooth) the ls-remote child's argv shows, in order: -c credential.helper= (the RESET), -c credential.helper=<ABS-JICHI-HARNESS> credential-helper (the SET), -c credential.useHttpPath=true; and its env carries JICHI_HARNESS_GIT_TOKEN_FILE + JICHI_HARNESS_CRED_RULE + JICHI_HARNESS_CRED_URL -- the SAME credential build as the push child, so an HTTPS remote's ls-remote consults the helper (pre-fix it had only the RESET + useHttpPath + CRED_URL)"
        fi
    fi
fi

# --- 28: P7 -- the pushKept gate (P7's tooth, 06:37 review) ------------------
# A ref refs/kept/planted is planted in the MIRROR (git update-ref after the
# fetch). (a) DEFAULT (no pushKept key): push exits 0 and the stand-in has NO
# refs/heads/$BR/kept/planted -- the gate is OFF, nothing kept is pushed.
# (b) pushKept=true (cfg11b.json, the same rehearsal with the key set): the
# same plant, push exits 0, and the stand-in HAS refs/heads/$BR/kept/planted
# at the same oid. The port of mirror.sh's ZD_PUSH_KEPT=1, now a real config
# key with a real default (off), not an absent one.
# Plant: refs/kept/planted -> the mirror's $BR/main commit (any commit the
# mirror already has; the kept refspec pushes refs/kept/* to
# refs/heads/$BR/kept/*).
_plant_oid=$(git -C "$tmp/dir11/mirror.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
if [ -z "$_plant_oid" ]; then
    t_fail "28: P7 could not resolve the mirror's $BR/main to plant refs/kept/planted: $(git -C "$tmp/dir11/mirror.git" for-each-ref 2>&1 | head_bytes 160)"
fi
# (a) default: plant, push, assert NO kept ref landed. FIRST clear any
# pre-existing kept ref in the stand-in: an earlier cell on the same
# rehearsal may have landed refs/heads/$BR/kept/planted, so (a) must read the
# standin's kept ref from a known-empty state (the runbook's "read the
# extracted set, not its size") -- without the clear, (a) asserts on stale
# state and reports a gate the code never opened.
git -C "$tmp/dir11/standin.git" update-ref -d "refs/heads/$BR/kept/planted" 2>/dev/null
git -C "$tmp/dir11/mirror.git" update-ref "refs/kept/planted" "$_plant_oid"
"$HBIN" mirror push --config "$tmp/cfg11.json" "$tmp/dir11/mirror.git" \
    < /dev/null > "$tmp/p27a" 2> "$tmp/p27a.err"
rc27a=$?
_kept_a=$(git -C "$tmp/dir11/standin.git" for-each-ref --format='%(objectname)' "refs/heads/$BR/kept/planted" 2>/dev/null)
# reset the plant for the (b) round
git -C "$tmp/dir11/mirror.git" update-ref -d "refs/kept/planted" 2>/dev/null
# (b) pushKept=true: (a) already landed the mirror's main into the stand-in,
# so add a FRESH team commit (and hfetch) to advance the mirror's main -- the
# kept ref is then planted at the NEW main, so (b)'s push genuinely carries it
# (not a no-op) and the kept plant can only land because pushKept=true.
# The _kept_* reads use for-each-ref, not `rev-parse`: a plain `rev-parse`
# against a MISSING ref still PRINTS the ref name to stdout (only the exit
# code says it is absent), so the `[ -n "$_kept_a" ]` test would read a
# non-existent kept ref as having landed. for-each-ref prints NOTHING for a
# missing ref and the oid for an existing one -- the read the cell needs.
git -C "$tmp/dir11/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team11: a $BR/main commit for the pushKept-on round"
hfetch "11"
_plant_b=$(git -C "$tmp/dir11/mirror.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
[ -n "$_plant_b" ] || _plant_b="$_plant_oid"
git -C "$tmp/dir11/mirror.git" update-ref "refs/kept/planted" "$_plant_b"
"$HBIN" mirror push --config "$tmp/cfg11b.json" "$tmp/dir11/mirror.git" \
    < /dev/null > "$tmp/p27b" 2> "$tmp/p27b.err"
rc27b=$?
_kept_b=$(git -C "$tmp/dir11/standin.git" for-each-ref --format='%(objectname)' "refs/heads/$BR/kept/planted" 2>/dev/null)
if [ "$rc27a" -ne 0 ]; then
    t_fail "28: P7 (a) the DEFAULT (pushKept absent) push did not exit 0 (rc=$rc27a): $(tr '\n' ' ' < "$tmp/p27a.err" | head_bytes 200)"
elif [ -n "$_kept_a" ]; then
    t_fail "28: P7 (a) the DEFAULT push LANDED a kept ref (refs/heads/$BR/kept/planted = $_kept_a) -- the gate must be OFF by default (mirror.sh's PUSH_KEPT=1, now a config key defaulting false)"
elif [ "$rc27b" -ne 0 ]; then
    t_fail "28: P7 (b) the pushKept=true push did not exit 0 (rc=$rc27b): $(tr '\n' ' ' < "$tmp/p27b.err" | head_bytes 200)"
elif [ -z "$_kept_b" ]; then
    t_fail "28: P7 (b) the pushKept=true push did NOT land refs/heads/$BR/kept/planted (the kept refspec is missing) -- the gate must push refs/kept/* when ON"
elif [ "$_kept_b" != "$_plant_b" ]; then
    t_fail "28: P7 (b) the pushed kept ref is at $_kept_b, not the planted $_plant_b"
else
    t_ok "28: P7 (the P7 fix's tooth) the pushKept gate: DEFAULT (no key) -- a planted refs/kept/planted is NOT pushed (stand-in has no refs/heads/$BR/kept/planted, exit 0); pushKept=true -- the same plant IS pushed to refs/heads/$BR/kept/planted at the same oid (exit 0) -- the port of mirror.sh's ZD_PUSH_KEPT=1, now a real config key with a real default"
fi

# --- 29: P4h -- a PLANTED hook in the mirror's hooks/ is refused (SV:1200.2)
# The push path's check 4 (hooks) had no driver cell: mirror_push runs the
# FIVE integrity checks before any git child, but no cell planted a hooks/
# file in the MIRROR (fetch's check 7 holds the fetch side, harness_mirror_
# fetch.sh cell 7). Plant a regular file in dir's mirror's hooks/ (the clean
# rehearsal whose live config was restored to the stored copy by cell 16 and
# whose big blobs are all in the remote by cell 11, so no OTHER check can
# refuse), run the push, assert exit 2, the stand-in UNCHANGED, and the
# EXACT refusal line naming check 4 (hooks) and the planted file -- exact
# because the pre-SV:1200.1 order was closedir() BEFORE refuse() and the
# file name came from the freed DIR buffer (ASan heap-use-after-free READ;
# a mangled or missing name is caught here, and under the SAN stage the
# old order aborts outright). Remove the plant after the cell.
_s_before29=$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)
printf '#!/bin/sh\ntouch /tmp/m5-pus-hook-fired\n' > "$tmp/dir/mirror.git/hooks/post-receive"
chmod 755 "$tmp/dir/mirror.git/hooks/post-receive"
rm -f /tmp/m5-pus-hook-fired
"$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/p29" 2> "$tmp/p29.err"
rc29=$?
rm -f "$tmp/dir/mirror.git/hooks/post-receive"
if [ "$rc29" -ne 2 ]; then
    t_fail "29: P4h a planted regular file in the mirror's hooks/ was not refused exit 2 (exit $rc29) -- check 4 (hooks) must refuse before any push: $(tr '\n' ' ' < "$tmp/p29.err" | head_bytes 200)"
elif ! grep -qF "check 4 (hooks): the mirror's hooks/ holds a regular file $tmp/dir/mirror.git/post-receive that the harness did not put there -- init lays the mirror out without arming it, so a hook there is a plant" "$tmp/p29.err" "$tmp/p29"; then
    t_fail "29: P4h the hooks refusal is not the EXACT check-4 line naming the planted file (the pre-SV:1200.1 closedir-before-refuse order read the name from a freed DIR buffer): $(tr '\n' ' ' < "$tmp/p29.err" | head_bytes 240)"
elif [ -e /tmp/m5-pus-hook-fired ]; then
    t_fail "29: P4h the planted hook FIRED -- the refusal must precede any git child"
elif [ "$_s_before29" != "$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$BR/main" 2>/dev/null)" ]; then
    t_fail "29: P4h the hooks refusal CHANGED the stand-in's $BR/main -- the push must not happen"
else
    t_ok "29: P4h a planted regular file in the mirror's hooks/ is refused exit 2 by the push's check 4 (hooks) with the EXACT line naming the planted file, the hook never fired, and the stand-in is UNCHANGED (the fetch-side tooth is harness_mirror_fetch.sh cell 7; the exact-string assert is the UAF order's bite)"
fi

# --- 31: P11r -- REAL git as the credential peer (M1061, red-first) ---------
# The hand-written credential cells (18/19/25) feed the identity BYTES
# themselves, so they pinned the helper against a fixture that does not
# behave like the peer: REAL git, under -c credential.useHttpPath=true,
# sends path= WITHOUT the leading slash (path=drive/remote.git), while the
# helper's classify_url kept the configured remote's path WITH it
# (/drive/remote.git) -- and the byte-exact comparison refused the
# configured remote itself, so `mirror push` against a real HTTPS remote
# could never authenticate (M1061). This cell drives REAL git as the peer:
# `git credential fill` with the reset (-c credential.helper=) + the
# two-word spec (-c credential.helper=<ABS-HBIN> credential-helper, exactly
# what jc_harness_push.c's credential_args builds) +
# -c credential.useHttpPath=true, fed a `url=` line on stdin. It works
# OFFLINE: a fill the helper ANSWERS exits 0 and prints the identity + the
# answer (git does not dial for `credential fill`); a fill the helper
# REFUSES exits 128 with `fatal: could not read Username for
# 'https://git.example.org/...': terminal prompts disabled` and the
# helper's refusal on stderr. (a) the configured remote: ANSWERED -- git
# exit 0, the capture carries REAL git's identity lines (protocol=https,
# host=git.example.org, path=drive/remote.git -- proving the peer is git,
# not a hand-written fixture), username=oauth2 and a password= line (the
# canary in $tmp/p31a is a DESIGNED exception of the cell 30 sweep, exactly
# like p19's stdout: the credential IS the helper's output channel).
# (b) a DIFFERENT project on the same host: REFUSED -- git exit 128, NO
# password= line, the helper's path-refusal naming the configured remote.
# This leg is GREEN before and after the M1061 fix: it is the control that
# pins the teeth of leg a (the guard compares byte-exactly on GIT'S form,
# it is not loosened to ignore the path).
_gitfill() {
    # $1 = the url line, $2 = out file, $3 = err file. REAL git's
    # `credential fill` with the harness as its credential helper (the
    # two-word spec), the identity env the push child carries, stdin closed
    # by the pipe. The driver's GIT_CONFIG_GLOBAL/SYSTEM=/dev/null exports
    # (above) already neutralise the operator's ambient gitconfig.
    printf 'url=%s\n\n' "$1" | \
        JICHI_HARNESS_GIT_TOKEN_FILE="$tmp/tokfile" JICHI_HARNESS_CRED_RULE=oauth2 \
        JICHI_HARNESS_CRED_URL="https://git.example.org/drive/remote.git" \
        git -c credential.helper= -c "credential.helper=$HBIN credential-helper" \
        -c credential.useHttpPath=true credential fill > "$2" 2> "$3"
}
_gitfill "https://git.example.org/drive/remote.git" "$tmp/p31a" "$tmp/p31a.err"
rc31a=$?
_gitfill "https://git.example.org/other/project.git" "$tmp/p31b" "$tmp/p31b.err"
rc31b=$?
if [ "$rc31a" -ne 0 ]; then
    t_fail "31: P11r (a) REAL git's credential fill for the CONFIGURED remote (url=https://git.example.org/drive/remote.git) exited $rc31a (want 0) -- the helper refused its own remote (M1061: the configured path keeps its leading '/', git sends none): $(tr '\n' ' ' < "$tmp/p31a.err" | head_bytes 200)"
elif ! grep -qF "protocol=https" "$tmp/p31a" || ! grep -qF "host=git.example.org" "$tmp/p31a" || ! grep -qF "path=drive/remote.git" "$tmp/p31a"; then
    t_fail "31: P11r (a) the fill's capture lacks REAL git's identity lines (protocol=https, host=git.example.org, path=drive/remote.git -- git's form, NO leading slash): $(tr '\n' ' ' < "$tmp/p31a" | head_bytes 160)"
elif ! grep -qF "username=oauth2" "$tmp/p31a" || ! grep -qF "password=" "$tmp/p31a"; then
    t_fail "31: P11r (a) the fill's capture lacks the helper's answer (username=oauth2, a password= line) -- the credential is the helper's output channel: $(tr '\n' ' ' < "$tmp/p31a" | head_bytes 160)"
elif [ "$rc31b" -ne 128 ]; then
    t_fail "31: P11r (b) REAL git's credential fill for a DIFFERENT project on the same host (url=https://git.example.org/other/project.git) exited $rc31b (want 128) -- the guard is not loosened, the refusal must hold: $(tr '\n' ' ' < "$tmp/p31b.err" | head_bytes 200)"
elif grep -qF "password=" "$tmp/p31b"; then
    t_fail "31: P11r (b) the different-project fill's capture carries a password= line -- the token went to a project the helper is not configured for: $(tr '\n' ' ' < "$tmp/p31b" | head_bytes 120)"
elif ! grep -qF "the requested path does not match the configured remote's path" "$tmp/p31b.err" || ! grep -qF "git.example.org/drive/remote.git" "$tmp/p31b.err"; then
    t_fail "31: P11r (b) the different-project fill's stderr lacks the helper's path-refusal naming the configured remote: $(tr '\n' ' ' < "$tmp/p31b.err" | head_bytes 240)"
else
    t_ok "31: P11r REAL git (credential fill, the reset + two-word spec + useHttpPath=true) as the credential peer: (a) the configured remote is ANSWERED -- git exit 0, the capture carries git's OWN identity (path=drive/remote.git, no leading slash) + username=oauth2 + a password= line; (b) a DIFFERENT project on the same host is REFUSED -- git exit 128, no password= line, the helper's path-refusal names the configured remote (the guard compares byte-exactly on git's form, not loosened; M1061)"
fi

# --- 30: P17 -- the canary never leaks (FINAL cell, holds across ALL) --------
# Grep BOTH the token canary and the key canary across every output/err file
# the driver produced AND the argv/env dump file(s). Assert NEITHER appears
# anywhere (the secret rule, behaviour 15): a test that handles a secret
# checks the canary never appears in a log, an argument list, or a child's
# environment. The designed exceptions are p15 (cell 17 P11), p19 (cell 23
# P11e) and p19h (cell 19 P11h): the credential IS the helper's output channel
# to git, by design -- the sweep covers every capture EXCEPT those stdouts
# plus p31a (cell 31 P11r leg a, REAL git's fill capture of the ANSWERED
# identity + answer -- the same designed channel), where the token MUST be
# present (so the helper is answered, not over-refused).
# The wrapper dumps live at $tmp/dN/dump (one per PATH entry); the shim's
# argv dump is the FILE $tmp/d19.
_leaked=""
for _f in "$tmp/i" "$tmp/i.err" "$tmp/i2" "$tmp/i2.err" "$tmp/i8" "$tmp/i8.err" "$tmp/i9" "$tmp/i9.err" \
          "$tmp/iE" "$tmp/iE.err" "$tmp/i10" "$tmp/i10.err" "$tmp/i11" "$tmp/i11.err" \
          "$tmp/i12" "$tmp/i12.err" \
          "$tmp/f" "$tmp/f.err" "$tmp/f2" "$tmp/f2.err" "$tmp/f8" "$tmp/f8.err" "$tmp/f9" "$tmp/f9.err" \
          "$tmp/f10" "$tmp/f10.err" "$tmp/f11" "$tmp/f11.err" \
          "$tmp/f12" "$tmp/f12.err" \
          "$tmp/p1" "$tmp/p1.err" "$tmp/p2a" "$tmp/p2a.err" "$tmp/p2b" "$tmp/p2b.err" \
          "$tmp/p3c" "$tmp/p3c.err" \
          "$tmp/p3" "$tmp/p3.err" "$tmp/p4" "$tmp/p4.err" "$tmp/p5" "$tmp/p5.err" \
          "$tmp/p6" "$tmp/p6.err" "$tmp/p7" "$tmp/p7.err" "$tmp/p8" "$tmp/p8.err" \
          "$tmp/p9" "$tmp/p9.err" "$tmp/p10" "$tmp/p10.err" "$tmp/p10setup" \
          "$tmp/p11" "$tmp/p11.err" "$tmp/p12" "$tmp/p12.err" "$tmp/p12tips" "$tmp/p13" "$tmp/p13.err" \
          "$tmp/p14" "$tmp/p14.err" "$tmp/p16s" "$tmp/p16s.err" "$tmp/p16e" "$tmp/p16e.err" \
          "$tmp/p17" "$tmp/p17.err" "$tmp/p18" "$tmp/p18.err" "$tmp/p20hg" "$tmp/p20hg.err" \
          "$tmp/p20h" "$tmp/p20h.err" "$tmp/p18g" "$tmp/p18g.err" "$tmp/p19h.err" \
          "$tmp/p20" "$tmp/p20.err" "$tmp/p20setup" "$tmp/p20seed" \
          "$tmp/p26" "$tmp/p26.err" "$tmp/p27a" "$tmp/p27a.err" "$tmp/p27b" "$tmp/p27b.err" \
          "$tmp/p29" "$tmp/p29.err" "$tmp/p32" "$tmp/p32.err" \
          "$tmp/p31a.err" "$tmp/p31b" "$tmp/p31b.err" \
          "$tmp/d1/dump" "$tmp/d2a/dump" "$tmp/d2b/dump" "$tmp/d3c/dump" "$tmp/d11/dump" "$tmp/d12/dump" \
          "$tmp/d13/dump" "$tmp/d14/dump" "$tmp/d26/dump" "$tmp/d19"; do
    [ -f "$_f" ] || continue
    if grep -qF "$TOKEN_CANARY" "$_f" 2>/dev/null; then
        _leaked="$_leaked token:$(basename "$_f")"
    fi
    if grep -qF "$_kc" "$_f" 2>/dev/null; then
        _leaked="$_leaked key:$(basename "$_f")"
    fi
done
if [ -n "$_leaked" ]; then
    t_fail "30: P17 the canary LEAKED into a driver-produced file (a secret must never appear in a log, an argument list, or a child's environment):$_leaked"
else
    t_ok "30: P17 the token canary and the key canary appear in NO output/err file and NO argv/env dump the driver produced (the secret rule holds across all cells; the helper's stdout is the designed channel)"
fi

t_done
