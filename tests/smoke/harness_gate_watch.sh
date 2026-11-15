#!/bin/sh
# smoke: `jichi-harness watch gate --config <cfg> [--log f] [--state f] [--out f]
# [--dry]` -- one tick of the gate watcher (M1153, batch H9a; the C89 port of
# the 2026-10-27 hardening-host drive's gate-watch.sh, whose comments name the
# four false-verdict incidents this driver holds: 07:55 the regate arm, 11:32
# the first false withdrawal, 13:32 the second false withdrawal, 13:47 the
# settled rule, 01:58 the busy-filter self-match). Plus `watch units`
# (the generated watcher's systemd units, the push-units idiom).
#
# M1153 (H9a). Design note (the contract, behaviour list B1-B16):
#   docs/internal/drive/2026-11-05-harness-port/notes/M1153.md
# Ported reference (read line by line; this driver holds its cells 1-15):
#   docs/internal/drive/2026-10-27-hardening-host/gate-watch.sh
#
# THE RED THIS IS HELD BY. On the tree before this batch the top-level verb
# `watch` does not exist in jichi-harness: every cell that runs `watch gate`
# or `watch units` gets `unknown command 'watch'` (exit 2) and no say line, no
# state word, no start, no digest. Every cell that asserts a say line, a state
# word, a start, a digest line, or an exit 0/1 is RED for that one reason, and
# the cells distinguish the reference's behaviours from each other by their
# say/state/digest bytes. The red tree's loader reads the config's `host`
# block point-wise and IGNORES the unknown keys (gateOnCalendar / gateRun /
# unitState / unitList / unitRun / pushRepo; the M1152 precedent), so the
# config always loads and the red lands on the unknown-command refusal --
# never a config refusal. The driver does not test the loader (it is not this
# driver's); it only asserts the config it feeds is the full gate config.
#
# WHAT IS CHECKED -- 22 cells, one t_ok/t_fail each:
#   1. the fixture guard: the 6-commit chain M0<-M1<-M2<-M3<-M4<-M5 + a SIDE
#      commit (child of M0) are laid out with push.git refs origin/master at
#      M2 and origin/<prefix>/main at M5, and the ancestor relationships the
#      cells rely on (SIDE on master's side but NOT under team-main, M2 under
#      M5, push2.git with NO origin/master) all hold. The fixture commits carry
#      FIXED dates (M1156 card 26, the cell 7 flake's teeth) and the cell
#      asserts M2's first-7 hex contains an a-f -- an all-digits M2:7 would make
#      cell 7's sentence name no other commit token and the tick withdraws.
#   2. no READY line -> exit 0, watch.out empty (the normal quiet tick).
#   3. wait-for-remote: READY at the side commit (in push.git, on master's
#      side, NOT under origin/<prefix>/main) -> the behaviour-2 say line
#      byte-exact, no state word, exit 0.
#   4. state `<M3-8> started` + unit-state shim rc 1 (dead) -> the behaviour-4
#      say line + a START (unit-run shim called with the --unit= option
#      --unit=jhp-gate-<M3-8> and the gateRun exec as SEPARATE words
#      <gateRun> <M3-8> 4, M1188).
#   5. state `<M3-8> landed` -> exit 0, watch.out EMPTY (behaviour 6's silence).
#   6. state `<M3-8> regate` -> the behaviour-5 say line byte-exact + a start
#      (unit-run shim called with the --unit= option and the exec as SEPARATE
#      words <gateRun> <M3-8> 6, M1188).
#   7. the 11:32 shape (A): READY at M4; a LATER line naming M4's 7-hex while
#      saying the earlier M2 "is WITHDRAWN" -> NOT withdrawn (reaches the
#      start arm; the say is NOT the withdraw line, the state gains no
#      `withdrawn`).
#   8. the 13:32 shape (J): the reference's exact sentence naming M3
#      (WITHDRAWN) and M2 beside the READY M4 -> NOT withdrawn (reaches the
#      start arm).
#   9. the genuine sole withdrawal (H): "<M4-7> is WITHDRAWN per the note
#      20261108-2237." -> the withdraw say line byte-exact, the state word
#      `withdrawn`, the behaviour-8 digest line (incl. the regate hint), and
#      the notify stub called.
#  10. the --dry half of 9: the same LOG -> the say line, NO state word, NO
#      digest line, NO stub call.
#  11. the a-f / date / decimal / other-commit cases: (i) "WITHDRAWN on
#      20261105" -> withdrawn (the date is not a commit token); (ii)
#      "WITHDRAWN, see 123456789012" -> still withdrawn (12-digit decimal, no
#      a-f, not a token); (iii) "WITHDRAWN (ref 9999999a)" -> NOT withdrawn
#      (9999999a is 8 hex WITH an a: other).
#  12. on-master: READY at M2 (master's tip) -> the behaviour-10 say line
#      byte-exact, the state word `on-master`, exit 0.
#  13. not-ff: READY at the side commit (neither an ancestor of master nor a
#      descendant) -> the say line, the state word `not-ff`, the notify stub
#      called with the reference's RED TEXT byte-exact (behaviour 9), exit 0.
#  14. no origin/master ref (a second bare repo without it) -> say
#      `no origin/master in push.git`, exit 1 (the reference's only exit-1).
#  15. the busy filter (behaviour 11, DD5): the unit-list stub prints
#      (a) jhp-gate-watch.service + jhp-gate-<M5-8>.service -> busy, the say
#      names ONLY the M5 unit (the 01:58 self-match's teeth); (b) only
#      jhp-gate-watch.service -> NOT busy (proceeds); (c) jhp-ci-<M5-8>.service
#      -> busy; (d) jhd-gate-<M5-8>.service (the OTHER drive's prefix) -> busy
#      (DD5's teeth); (e) jhp-gate-watch.timer + jhd-gate-watch.service (both
#      watches' own units) -> NOT busy.
#  16. the team-smoke wait (behaviour 12): a REAL background process whose
#      argv contains `tests/smoke/` and whose cwd is $DIR/team -> the tick
#      waits (the say names the pid); after it is killed the next tick
#      proceeds. The process is killed at the cell's end and in the trap
#      (no leak).
#  17. the start (behaviour 13): READY at M5 (the happy path, master at M2),
#      unit-run stub -> the state word `started`, the say line byte-exact,
#      and the stub's argv in the reference's token order (M1188): the
#      `--unit=jhp-gate-<M5-8>` option, the `-p` property line `Description=
#      jichi drive gate on <M5-8> (BATCH 17)`, and the exec as SEPARATE words
#      (`<gateRun>`, `<M5-8>`, `17` -- not one space-joined word), plus the
#      stub's marker line in watch.out.
#  18. unit-run stub rc 7 -> say `systemd-run failed (rc=7)`, exit 1, the
#      state word `started` STILL PRESENT (behaviour 13's KEPT half).
#  19. --dry -> the DRY say line, the stub NOT called, no state word.
#  20. `watch units`: the generated jhp-gate-watch.timer carries
#      OnCalendar=<gateOnCalendar>, Persistent=true, AccuracySec=30s; the
#      service's ExecStart names the watch command with the config's argv0
#      path; an absent gateOnCalendar refuses (exit 2, names it); bare
#      `watch` exits 2 naming `gate`; `watch frob` exits 2; `watch gate` with
#      a config that has NO host block exits 2 naming `no host block`. (On the
#      red tree every `watch` variant is the unknown command, exit 2 -- the
#      cell asserts the green shape; it passes only once wave B builds the
#      verb.)
#  21. the --log default (M1156): a config whose dir is a fixture dir holding
#      LOG.md, ticked with NO --log -> the tick reads <dir>/LOG.md (it does NOT
#      refuse for lacking --log: rc 0, not the unfixed rc 1) and the
#      wait-for-remote say line NAMES the log's leaf (LOG.md:) beside the line
#      number; the same say with an explicit --log <path-with-slashes> names
#      THAT path's leaf.
#  21b. the empty --log "" half (M1156, the H9a1 independent review's WEAK gap):
#      the same fixture as 21, ticked with --log "" EXPLICITLY (an empty string,
#      not an absent flag) -> rc 0 and the wait-for-remote line byte-exact, the
#      leaf named from the DEFAULT path <dir>/LOG.md (LOG.md:1). This pins the
#      `|| log[0] == '\0'` half of the branch `log == NULL || log[0] == '\0'`
#      in src/harness/jc_hp_watchgate.c that the H9a1 WEAK found unpinned: its
#      teeth mutant (drop only that half) makes the empty --log tick QUIET (rc 0,
#      no say line).
#
# THE FIXTURE. Under $tmp: a REAL git scratch working repo with the 6-commit
# chain M0<-M1<-M2<-M3<-M4<-M5 plus a SIDE commit (a child of M0) -- the
# note's "M1 (behind master, a side branch point)" -- that is on master's
# side (an ancestor of M2) but NOT an ancestor of team-main; two bare repos
# push.git / push2.git (clones of the scratch) whose refs the driver places
# with `git update-ref`: push.git has refs/remotes/origin/master at M2 and
# refs/remotes/origin/<prefix>/main at M5; push2.git is the same but WITHOUT
# the origin/master ref (cell 14). The LOG is a scratch file rewritten per
# cell; state / watch.out are scratch files named by --state/--out. The
# config JSON is driver-written (dir = $tmp/dir with team/ and gate/, the
# host block: pushHome, gateHome, gateOnCalendar, gateRun, unitState,
# unitList, unitRun, notifyCommand; plus modelKeyFile / gitTokenFile canary
# files as in harness_notify.sh). The stub commands the config names for the
# CHANGED host halves (behaviours 4/11/13): unit-state.sh (is-active: rc 0
# active / rc 1 dead, set by $tmp/usrc), unit-list.sh (prints the unit names
# the filter must classify, from $tmp/units), unit-run.sh (systemd-run
# stand-in: records its argv one per line to $tmp/ur-args, rc set by
# $tmp/urrc), gate.sh (the gateRun exec: prints a marker), notify.sh (records
# its args to $tmp/notified). The driver runs the tree's binary by path
# ($HBIN, the third_program idiom), never a PATH-resolved jichi-harness, and
# -- for smoke_lint check 5 -- also runs the tree's "$BIN" once. No timeout
# wrapper (a 6-commit chain and one background sleep are the only wall time).
# git children carry -c user.name/-c user.email (M2-neutral) and git -C
# everywhere (no cd); EVERY commit-making call also carries a FIXED
# GIT_AUTHOR_DATE/GIT_COMMITTER_DATE (M1156 card 26) so the chain's shas -- and
# therefore the cells' expectations -- do not depend on the operator's clock.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"

t_plan 25
smoke_home
tmp=$(smoke_tmp)

# The background team-smoke (cell 16) must not leak: kill it on every exit
# path (smoke_cleanup is _smoke.sh's own EXIT trap; this wraps it).
GC_PID=""
gw_cleanup() {
    [ -n "$GC_PID" ] && kill "$GC_PID" 2>/dev/null
    wait "$GC_PID" 2>/dev/null
    smoke_cleanup
}
trap gw_cleanup EXIT INT TERM

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

# A fixture failure emits one t_fail per cell so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt 22 ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

# --- the fixture: the 6-commit chain + side commit, two bare repos ----------
PFX=drive/test-w
DIR="$tmp/dir"
GH="$DIR/gate"
UHOME="$tmp/uhome"
if ! mkdir -p "$DIR/team" "$GH" "$UHOME"; then
    fixture_fail "the scratch dirs could not be made"
fi

WORK="$tmp/work"
# -b main: the fixture's later `checkout -q main` needs the main-line branch to
# be named main regardless of the operator's init.defaultBranch.
if ! git init -q -b main "$WORK"; then
    fixture_fail "git init of the scratch work repo failed"
fi
# The 6-commit chain M0<-M1<-M2<-M3<-M4<-M5; the first five are main-line
# commits, M5 is a merge (below). Capture each commit's SHA as it is made, so
# the side branches need no fragile parent-walks.
_c() { # $1 = message; $2 = the fixed GIT_AUTHOR/COMMITTER date; commits $1 and echoes the new SHA
    printf 'file %s\n' "$1" > "$WORK/f"
    git -C "$WORK" add f
    GIT_AUTHOR_DATE="$2" GIT_COMMITTER_DATE="$2" \
        git -C "$WORK" -c user.name="Harness Smoke" \
        -c user.email="harness-smoke@example.com" commit -q -m "$1"
    git -C "$WORK" rev-parse HEAD
}
# The fixture's commits carry FIXED dates (M1156 card 26): the driver must not
# depend on the operator's clock, because cell 7's sentence names M2's first-7
# hex as "the earlier <m2:7> is WITHDRAWN" and the withdrawal engine only reads
# it as an OTHER commit token when it contains an a-f (the rule is 7-40 lowercase
# hex with at least one a-f; test_watchgate.c case 9). With unfixed dates M2's
# sha varies per run and is all digits ~3.5% of the time, in which case the
# sentence names no other token and the tick WITHDRAWS -- cell 7 flaky red.
# Pinning the dates makes M2 (and the whole chain) stable; the m27 assertion in
# cell 1 proves the pinned M2:7 carries an a-f.
M0=$(_c M0 "2026-11-09T00:00:00Z"); M1=$(_c M1 "2026-11-09T00:00:01Z")
M2=$(_c M2 "2026-11-09T00:00:02Z"); M3=$(_c M3 "2026-11-09T00:00:03Z")
M4=$(_c M4 "2026-11-09T00:00:04Z")
# SB: a side commit off M1, merged into the team tip M5 below -- under the
# team ref but diverged from master (the not-ff subject, cell 13). WFR: a side
# commit off M1 that is NEVER merged -- in push.git but not under the team ref
# (the wait-for-remote subject, cell 3). (The note's single "M1" is both; two
# distinct commits carry the two different topologies.)
git -C "$WORK" checkout -q -b side "$M1"
printf 'side\n' > "$WORK/s"
git -C "$WORK" add s
GIT_AUTHOR_DATE="2026-11-09T00:00:05Z" GIT_COMMITTER_DATE="2026-11-09T00:00:05Z" \
    git -C "$WORK" -c user.name="Harness Smoke" \
    -c user.email="harness-smoke@example.com" commit -q -m "side"
SB=$(git -C "$WORK" rev-parse HEAD)
git -C "$WORK" checkout -q main
git -C "$WORK" checkout -q -b wfr "$M1"
printf 'wfr\n' > "$WORK/w"
git -C "$WORK" add w
GIT_AUTHOR_DATE="2026-11-09T00:00:06Z" GIT_COMMITTER_DATE="2026-11-09T00:00:06Z" \
    git -C "$WORK" -c user.name="Harness Smoke" \
    -c user.email="harness-smoke@example.com" commit -q -m "wfr"
WFR=$(git -C "$WORK" rev-parse HEAD)
git -C "$WORK" checkout -q main
# M5: the team tip -- a merge of main (M4) and side (SB), so it is under
# origin/master's line (M2 is its ancestor) AND carries SB.
GIT_AUTHOR_DATE="2026-11-09T00:00:07Z" GIT_COMMITTER_DATE="2026-11-09T00:00:07Z" \
    git -C "$WORK" -c user.name="Harness Smoke" \
    -c user.email="harness-smoke@example.com" merge --no-ff -m "M5" side
M5=$(git -C "$WORK" rev-parse HEAD)

git clone -q --bare "$WORK" "$UHOME/push.git"
git -C "$UHOME/push.git" update-ref refs/remotes/origin/master "$M2"
git -C "$UHOME/push.git" update-ref "refs/remotes/origin/$PFX/main" "$M5"
git clone -q --bare "$WORK" "$UHOME/push2.git"
git -C "$UHOME/push2.git" update-ref "refs/remotes/origin/$PFX/main" "$M5"
# (push2.git keeps NO refs/remotes/origin/master: cell 14.)

# --- the stub commands (behaviours 4/11/13's CHANGED host halves) ----------
printf '#!/bin/sh\nrc=$(cat "%s/usrc" 2>/dev/null); [ -n "$rc" ] || rc=1\nexit "$rc"\n' \
    "$tmp" > "$UHOME/unit-state.sh"
chmod 755 "$UHOME/unit-state.sh"
# The unit-list stub prints $tmp/units (one unit name per line) in the REAL
# shape of `systemctl --user list-units --no-legend` (M1178): two leading
# spaces, the unit, then the LOAD ACTIVE SUB DESCRIPTION columns. Until M1178
# it printed the bare names, and the tick's parse, which read the line's first
# space-delimited token, passed here while reading an EMPTY token from every
# real line: a running gate was never busy (measured on the host 2026-11-13).
printf '#!/bin/sh\nsed -e "s/^/  /" -e "s/\\$/ loaded active running stub unit/" "%s/units" 2>/dev/null\n' \
    "$tmp" > "$UHOME/unit-list.sh"
chmod 755 "$UHOME/unit-list.sh"
{
    # The unit-run stub records its argv ONE ELEMENT PER LINE (the brief's
    # exact-line grep teeth, M1188). The format MUST be quoted ('%s\n'): an
    # unquoted %s\n in dash expands as the format %sn and prints a literal n
    # with NO newline, so every arg landed on ONE line and the -qxF assertions
    # could never match (latent bug, found M1188).
    printf '#!/bin/sh\nfor _a in "$@"; do printf '"'"'%%s\\n'"'"' "$_a" >> "%s/ur-args"; done\n' "$tmp"
    printf 'echo "GATE-UNIT-RAN: marker for this driver"\n'
    printf 'rc=$(cat "%s/urrc" 2>/dev/null); [ -n "$rc" ] || rc=0\nexit "$rc"\n' "$tmp"
} > "$UHOME/unit-run.sh"
chmod 755 "$UHOME/unit-run.sh"
printf '#!/bin/sh\necho "GATE-RUN-EXEC: $*"\n' \
    > "$UHOME/gate.sh"
chmod 755 "$UHOME/gate.sh"
printf '#!/bin/sh\nfor _a in "$@"; do printf %%s\\n "$_a" >> "%s/notified"; done\nexit 0\n' \
    "$tmp" > "$UHOME/notify.sh"
chmod 755 "$UHOME/notify.sh"

# --- the canary secret files (harness_notify.sh idiom) ----------------------
KEYCANARY=$(od -An -N20 -tx1 /dev/urandom 2>/dev/null | tr -d ' \n'); [ -n "$KEYCANARY" ] || KEYCANARY="canarykey$$_"
TOKCANARY=$(od -An -N20 -tx1 /dev/urandom 2>/dev/null | tr -d ' \n'); [ -n "$TOKCANARY" ] || TOKCANARY="canarytok$$_"
printf '%s\n' "$KEYCANARY" > "$tmp/kfile"; chmod 600 "$tmp/kfile"
printf '%s\n' "$TOKCANARY" > "$tmp/tokfile"; chmod 600 "$tmp/tokfile"

# --- the config (driver-written; the red loader ignores the unknown keys) ---
OC='*-*-* *:07/10:00'
write_cfg() { # $1 = the file; the full gate config (the host block names the
              # stubs + gateOnCalendar + gateRun + the two canary secret files).
    cat > "$1" <<EOF
{"dir":"$DIR",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"drive":{"branchPrefix":"$PFX","gitName":"Harness Smoke","gitEmail":"harness-smoke@example.com"},
"host":{"pushHome":"$UHOME","gateHome":"$GH","gateOnCalendar":"$OC","gateRun":"$UHOME/gate.sh",
"unitState":"$UHOME/unit-state.sh","unitList":"$UHOME/unit-list.sh","unitRun":"$UHOME/unit-run.sh","notifyCommand":"$UHOME/notify.sh"}}
EOF
}
write_cfg "$tmp/cfg.json"
# The no-host-block config (cell 20c).
cat > "$tmp/cfg-nohost.json" <<EOF
{"dir":"$DIR",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"drive":{"branchPrefix":"$PFX","gitName":"Harness Smoke","gitEmail":"harness-smoke@example.com"}}
EOF
# The host-block WITHOUT gateOnCalendar (cell 20b's refusal half). Remove ONLY
# the key (not the host line: pushHome / gateHome / gateRun share it, and
# deleting the line would leave the block's opening brace unmatched -> invalid
# JSON, which the loader would refuse for the wrong reason).
write_cfg "$tmp/cfg-nocal.json"
sed 's/"gateOnCalendar":"[^"]*",//' "$tmp/cfg-nocal.json" > "$tmp/cfg-nocal2.json"
mv "$tmp/cfg-nocal2.json" "$tmp/cfg-nocal.json"
# The config for push2.git (cell 14: NO origin/master ref).
cat > "$tmp/cfg-push2.json" <<EOF
{"dir":"$DIR",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"drive":{"branchPrefix":"$PFX","gitName":"Harness Smoke","gitEmail":"harness-smoke@example.com"},
"host":{"pushHome":"$UHOME","gateHome":"$GH","gateOnCalendar":"$OC","gateRun":"$UHOME/gate.sh",
"unitState":"$UHOME/unit-state.sh","unitList":"$UHOME/unit-list.sh","unitRun":"$UHOME/unit-run.sh","notifyCommand":"$UHOME/notify.sh","pushRepo":"$UHOME/push2.git"}}
EOF

# --- helpers -----------------------------------------------------------------
fget() { [ -f "$1" ] && cat "$1" 2>/dev/null; }
# state_word FILE S8 -- the LAST state line beginning "<S8> ": print the
# line's word, or nothing (behaviour 3's read: grep ^<s8> state | tail -1 |
# awk '{print $2}').
state_word() {
    grep "^$2 " "$1" 2>/dev/null | tail -1 | awk '{print $2}'
}
# state_has FILE WORD -- 1 if any state line carries the word.
state_has() {
    grep -q " $1" "$2" 2>/dev/null
}
# reset_stub -- clear the CHANGED-host shims' recordings + reset the unit list
# to the watcher's own unit (the neutral state).
reset_stub() {
    : > "$tmp/ur-args" 2>/dev/null || :
    : > "$tmp/urrc" 2>/dev/null || :
    : > "$tmp/usrc" 2>/dev/null || :
    : > "$tmp/notified" 2>/dev/null || :
    printf 'jhp-gate-watch.service\n' > "$tmp/units" 2>/dev/null || :
}

# --- 1: the fixture guard ----------------------------------------------------
if [ ! -x "$HBIN" ]; then
    t_fail "1: the harness binary $HBIN is not built (run \`make jichi-harness\` first) -- the fixture never ran"
else
    _g=1; _gm=""
    git -C "$UHOME/push.git" rev-parse --verify -q "$M5" >/dev/null || { _g=0; _gm="$_gm M5 missing"; }
    git -C "$UHOME/push.git" merge-base --is-ancestor "$SB" "refs/remotes/origin/$PFX/main" || { _g=0; _gm="$_gm SB not under team-main"; }
    git -C "$UHOME/push.git" merge-base --is-ancestor "$SB" refs/remotes/origin/master && { _g=0; _gm="$_gm SB on master's line (would be on-master, not not-ff)"; }
    git -C "$UHOME/push.git" merge-base --is-ancestor refs/remotes/origin/master "$SB" && { _g=0; _gm="$_gm master under SB (SB is a descendant, not not-ff)"; }
    git -C "$UHOME/push.git" rev-parse --verify -q "$WFR^{commit}" >/dev/null || { _g=0; _gm="$_gm WFR not in push.git"; }
    git -C "$UHOME/push.git" merge-base --is-ancestor "$WFR" "refs/remotes/origin/$PFX/main" && { _g=0; _gm="$_gm WFR under team-main (would not wait)"; }
    git -C "$UHOME/push.git" merge-base --is-ancestor "$M5" "refs/remotes/origin/$PFX/main" || { _g=0; _gm="$_gm M5 not under team-main"; }
    git -C "$UHOME/push.git" merge-base --is-ancestor "$M2" refs/remotes/origin/master || { _g=0; _gm="$_gm M2 not at origin/master"; }
    git -C "$UHOME/push.git" merge-base --is-ancestor "$M2" "$M5" || { _g=0; _gm="$_gm M2 not ancestor of M5"; }
    git -C "$UHOME/push.git" merge-base --is-ancestor "$M5" refs/remotes/origin/master && { _g=0; _gm="$_gm M5 under master (the start would be on-master)"; }
    git -C "$UHOME/push2.git" rev-parse --verify -q refs/remotes/origin/master >/dev/null && { _g=0; _gm="$_gm push2 has origin/master"; }
    # The m27 pin (M1156 card 26): the fixture's M2 commit now carries a FIXED
    # date (see the chain above), so its first-7 hex is stable per run. Cell 7's
    # sentence "the earlier <m2_7> is WITHDRAWN" is read as an OTHER commit token
    # (-> not withdrawn) only when that 7-hex contains an a-f -- the withdrawal
    # engine's token rule is 7-40 lowercase hex with at least one a-f (pinned by
    # test_watchgate.c case 9). With the old unfixed dates M2 varied per run and
    # was all digits ~3.5% of the time, in which case the sentence named no other
    # token and the tick WITHDREW -- the cell 7 flake. Assert the pin here, where
    # M2 is first used, so a future drift in the pinned date cannot re-open it.
    _m27=$(printf '%.7s' "$M2")
    G=/usr/bin/grep
    [ -x "$G" ] || G=grep
    printf '%s' "$_m27" | $G -q '[a-f]' || { _g=0; _gm="$_gm M2:7=$_m27 is all digits (no a-f) -- the cell 7 flake: its sentence names no other commit token and the tick withdraws"; }
    if [ "$_g" -eq 1 ]; then
        t_ok "1: the fixture -- 6-commit chain M0..M5 (M5 a merge of M4+side) with push.git refs origin/master at M2 and origin/$PFX/main at M5; SB (under team-main, diverged from master) for the not-ff cell and WFR (in push.git, not under team-main) for the wait-for-remote cell; push2.git without origin/master -- all hold; the fixture commits carry FIXED dates (M1156 card 26) and M2:7=$_m27 contains an a-f (the cell 7 flake's teeth: an all-digits M2:7 would make its sentence name no other commit token)"
    else
        t_fail "1: the git fixture is wrong:$_gm -- the cells that assert on it are void"
    fi
fi

m1_7=$(printf '%.7s' "$M1"); m2_7=$(printf '%.7s' "$M2")
m3_7=$(printf '%.7s' "$M3"); m4_7=$(printf '%.7s' "$M4")
m5_7=$(printf '%.7s' "$M5"); side_7=$(printf '%.7s' "$SB"); wfr_7=$(printf '%.7s' "$WFR")
m1_8=$(printf '%.8s' "$M1"); m2_8=$(printf '%.8s' "$M2")
m3_8=$(printf '%.8s' "$M3"); m4_8=$(printf '%.8s' "$M4")
m5_8=$(printf '%.8s' "$M5"); side_8=$(printf '%.8s' "$SB"); wfr_8=$(printf '%.8s' "$WFR")

# --- 2: no READY line -> exit 0, watch.out empty -----------------------------
reset_stub
LOG="$tmp/log2"
printf 'a log line with no ready marker\n' > "$LOG"
OUT2="$GH/out2"; ST2="$GH/st2"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST2" --out "$OUT2" \
    < /dev/null > "$tmp/c2.out" 2> "$tmp/c2.err"
rc2=$?
if [ "$rc2" -ne 0 ]; then
    t_fail "2: no-READY-line tick exited $rc2 (want 0) -- on the red tree the \`watch\` verb does not exist: $(tr '\n' ' ' < "$tmp/c2.err" | head_bytes 160)"
elif [ -s "$OUT2" ] || [ -s "$ST2" ]; then
    t_fail "2: the quiet tick wrote watch.out or state (it must write NOTHING): out=$(fget "$OUT2" | head_bytes 160) state=$(fget "$ST2" | head_bytes 160)"
else
    t_ok "2: no READY line -> exit 0, watch.out empty (the normal quiet tick, 9 of 10 times)"
fi

# --- 3: wait-for-remote (READY at the side commit, not under team-main) ------
# The wait line carries the LOG-DERIVED 8 (the reference's :40 fires BEFORE :42
# re-resolves s from the full sha, so $s is still the LOG's %.8s; a 7-char
# READY is 7 chars -- the port is faithful). The port prints s8 truncated to
# the READY string's length here, which equals $wfr_7 for this 7-char fixture.
reset_stub
LOG="$tmp/log3"
printf 'BATCH 3 READY at %s\n' "$wfr_7" > "$LOG"
OUT3="$GH/out3"; ST3="$GH/st3"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST3" --out "$OUT3" \
    < /dev/null > "$tmp/c3.out" 2> "$tmp/c3.err"
rc3=$?
_exp3="BATCH 3 READY at $wfr_7 (log3:1): not on the remote's team branch yet; waiting for the push timer"
if [ "$rc3" -ne 0 ]; then
    t_fail "3: the wait-for-remote tick exited $rc3 (want 0) -- on the red tree the \`watch\` verb does not exist: $(tr '\n' ' ' < "$tmp/c3.err" | head_bytes 160)"
elif ! grep -qF "$_exp3" "$tmp/c3.out"; then
    t_fail "3: the wait-for-remote say line is not byte-exact (want \`$_exp3\`): $(tr '\n' '|' < "$tmp/c3.out" | head_bytes 240)"
elif [ -s "$ST3" ]; then
    t_fail "3: the wait-for-remote tick wrote a state word (it must not): state=$(fget "$ST3" | head_bytes 160)"
else
    t_ok "3: wait-for-remote -- READY at $wfr_7 (in push.git, a side commit never merged into the team tip, so not under origin/$PFX/main) -> the behaviour-2 say line byte-exact, no state word, exit 0"
fi

# --- 4: state started + dead -> the behaviour-4 say + a START ---------------
reset_stub
printf '1\n' > "$tmp/usrc"   # dead
LOG="$tmp/log4"
printf 'BATCH 4 READY at %s\n' "$m3_7" > "$LOG"
OUT4="$GH/out4"; ST4="$GH/st4"
printf '%s started 2026-11-08 00:00:00 +0000\n' "$m3_8" > "$ST4"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST4" --out "$OUT4" \
    < /dev/null > "$tmp/c4.out" 2> "$tmp/c4.err"
rc4=$?
_exp4="the gate on $m3_8 died without a verdict (jhp-gate-$m3_8 is not active): gating it again"
_ok4=1; _4_msg=""
if [ "$rc4" -ne 0 ]; then
    _ok4=0; _4_msg="rc=$rc4"
elif ! grep -qF "$_exp4" "$tmp/c4.out"; then
    _ok4=0; _4_msg="say not byte-exact (want \`$_exp4\`): $(tr '\n' '|' < "$tmp/c4.out" | head_bytes 160)"
elif ! grep -qF -- "--unit=jhp-gate-$m3_8" "$tmp/ur-args" 2>/dev/null; then
    _ok4=0; _4_msg="no START (the --unit= option --unit=jhp-gate-$m3_8 is not in ur-args): $(tr '\n' '|' < "$tmp/ur-args" 2>/dev/null | head_bytes 120)"
elif ! grep -qxF -- "-p" "$tmp/ur-args" 2>/dev/null; then
    _ok4=0; _4_msg="the description is not the -p PROPERTY (no standalone -p line in ur-args; without it Description=... is a bare positional systemd-run would exec): $(tr '\n' '|' < "$tmp/ur-args" 2>/dev/null | head_bytes 120)"
elif ! grep -qxF "$UHOME/gate.sh" "$tmp/ur-args" 2>/dev/null \
      || ! grep -qxF "$m3_8" "$tmp/ur-args" 2>/dev/null \
      || ! grep -qxF "4" "$tmp/ur-args" 2>/dev/null; then
    _ok4=0; _4_msg="the exec is not SEPARATE words (<gateRun>, $m3_8, 4 -- each its own line in ur-args): $(tr '\n' '|' < "$tmp/ur-args" 2>/dev/null | head_bytes 120)"
elif grep -qF "$UHOME/gate.sh $m3_8 4" "$tmp/ur-args" 2>/dev/null; then
    _ok4=0; _4_msg="the exec is still ONE space-joined word (<gateRun> $m3_8 4) instead of separate words: $(tr '\n' '|' < "$tmp/ur-args" 2>/dev/null | head_bytes 120)"
fi
if [ "$_ok4" -eq 1 ]; then
    t_ok "4: state \`<M3-8> started\` + unit-state shim rc 1 (dead) -> the behaviour-4 say line byte-exact and a START (unit-run shim called with the --unit=jhp-gate-$m3_8 option and the gateRun exec as SEPARATE words <gateRun> $m3_8 4)"
else
    t_fail "4: the started-dead re-gate misbehaved -- $_4_msg -- on the red tree the \`watch\` verb does not exist"
fi

# --- 5: state landed -> exit 0, watch.out EMPTY (behaviour 6's silence) -----
reset_stub
LOG="$tmp/log5"
printf 'BATCH 5 READY at %s\n' "$m3_7" > "$LOG"
OUT5="$GH/out5"; ST5="$GH/st5"
printf '%s landed 2026-11-08 00:00:00 +0000\n' "$m3_8" > "$ST5"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST5" --out "$OUT5" \
    < /dev/null > "$tmp/c5.out" 2> "$tmp/c5.err"
rc5=$?
if [ "$rc5" -ne 0 ]; then
    t_fail "5: the landed-silence tick exited $rc5 (want 0) -- on the red tree the \`watch\` verb does not exist: $(tr '\n' ' ' < "$tmp/c5.err" | head_bytes 160)"
elif [ -s "$OUT5" ] || [ -s "$tmp/ur-args" ]; then
    t_fail "5: the landed-state tick wrote watch.out or started a gate (behaviour 6: any other state word = exit 0, say nothing): out=$(fget "$OUT5" | head_bytes 160) ur-args=$(fget "$tmp/ur-args" | head_bytes 160)"
else
    t_ok "5: state \`<M3-8> landed\` -> exit 0, watch.out EMPTY (behaviour 6's silence: any other state word, the verdict stands, say nothing)"
fi

# --- 6: the regate arm (the 07:55 incident) --------------------------------
reset_stub
LOG="$tmp/log6"
printf 'BATCH 6 READY at %s\n' "$m3_7" > "$LOG"
OUT6="$GH/out6"; ST6="$GH/st6"
printf '%s regate 2026-11-08 00:00:00 +0000\n' "$m3_8" > "$ST6"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST6" --out "$OUT6" \
    < /dev/null > "$tmp/c6.out" 2> "$tmp/c6.err"
rc6=$?
_exp6="BATCH 6 READY at $m3_8: gating it again (gate/state says regate: the earlier verdict was withdrawn)"
_ok6=1; _6_msg=""
if [ "$rc6" -ne 0 ]; then
    _ok6=0; _6_msg="rc=$rc6"
elif ! grep -qF "$_exp6" "$tmp/c6.out"; then
    _ok6=0; _6_msg="say not byte-exact (want \`$_exp6\`): $(tr '\n' '|' < "$tmp/c6.out" | head_bytes 160)"
elif ! grep -qF -- "--unit=jhp-gate-$m3_8" "$tmp/ur-args" 2>/dev/null; then
    _ok6=0; _6_msg="no START (the --unit= option --unit=jhp-gate-$m3_8 is not in ur-args): $(tr '\n' '|' < "$tmp/ur-args" 2>/dev/null | head_bytes 120)"
elif ! grep -qxF -- "-p" "$tmp/ur-args" 2>/dev/null; then
    _ok6=0; _6_msg="the description is not the -p PROPERTY (no standalone -p line in ur-args; without it Description=... is a bare positional systemd-run would exec): $(tr '\n' '|' < "$tmp/ur-args" 2>/dev/null | head_bytes 120)"
elif ! grep -qxF "$UHOME/gate.sh" "$tmp/ur-args" 2>/dev/null \
      || ! grep -qxF "$m3_8" "$tmp/ur-args" 2>/dev/null \
      || ! grep -qxF "6" "$tmp/ur-args" 2>/dev/null; then
    _ok6=0; _6_msg="the exec is not SEPARATE words (<gateRun>, $m3_8, 6 -- each its own line in ur-args): $(tr '\n' '|' < "$tmp/ur-args" 2>/dev/null | head_bytes 120)"
elif grep -qF "$UHOME/gate.sh $m3_8 6" "$tmp/ur-args" 2>/dev/null; then
    _ok6=0; _6_msg="the exec is still ONE space-joined word (<gateRun> $m3_8 6) instead of separate words: $(tr '\n' '|' < "$tmp/ur-args" 2>/dev/null | head_bytes 120)"
fi
if [ "$_ok6" -eq 1 ]; then
    t_ok "6: state \`<M3-8> regate\` -> the behaviour-5 say line byte-exact and a start (the 07:55 regate arm: the earlier verdict was withdrawn, gate it again; the unit-run shim got the --unit=jhp-gate-$m3_8 option and the exec as SEPARATE words <gateRun> $m3_8 6)"
else
    t_fail "6: the regate arm misbehaved -- $_6_msg -- on the red tree the \`watch\` verb does not exist"
fi

# --- 7: the 11:32 shape (A): a later line naming M4 + M2 WITHDRAWN ----------
reset_stub
LOG="$tmp/log7"
printf 'BATCH 7 READY at %s\n' "$m4_7" > "$LOG"
printf 'pause-check: the batch at %s is still READY; the earlier %s is WITHDRAWN.\n' "$m4_7" "$m2_7" >> "$LOG"
OUT7="$GH/out7"; ST7="$GH/st7"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST7" --out "$OUT7" \
    < /dev/null > "$tmp/c7.out" 2> "$tmp/c7.err"
rc7=$?
_ok7=1; _7_msg=""
if [ "$rc7" -ne 0 ]; then
    _ok7=0; _7_msg="rc=$rc7"
elif grep -qF "is WITHDRAWN by a later line" "$tmp/c7.out"; then
    _ok7=0; _7_msg="read as a WITHDRAWAL (the ambiguous sentence named M4 + M2): $(tr '\n' '|' < "$tmp/c7.out" | head_bytes 160)"
elif state_has withdrawn "$ST7"; then
    _ok7=0; _7_msg="wrote the state word \`withdrawn\` (it must not): state=$(fget "$ST7" | head_bytes 120)"
elif ! grep -qF "jhp-gate-$m4_8" "$tmp/ur-args" 2>/dev/null; then
    _ok7=0; _7_msg="did not reach the start arm (unit jhp-gate-$m4_8 not in ur-args): $(tr '\n' '|' < "$tmp/c7.out" | head_bytes 160)"
fi
if [ "$_ok7" -eq 1 ]; then
    t_ok "7: the 11:32 shape (A) -- READY at M4, a later line naming M4's 7-hex while saying the earlier M2 is WITHDRAWN: NOT withdrawn (reaches the start arm; the say is NOT the withdraw line, the state gains no \`withdrawn\`)"
else
    t_fail "7: the 11:32 shape misbehaved -- $_7_msg -- on the red tree the \`watch\` verb does not exist"
fi

# --- 8: the 13:32 shape (J): the reference's exact sentence ----------------
reset_stub
LOG="$tmp/log8"
printf 'BATCH 8 READY at %s\n' "$m4_7" > "$LOG"
printf 'The earlier submissions at %s (WITHDRAWN) and %s will not be gated; the gate runs on %s per the READY line.\n' \
    "$m3_7" "$m2_7" "$m4_7" >> "$LOG"
OUT8="$GH/out8"; ST8="$GH/st8"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST8" --out "$OUT8" \
    < /dev/null > "$tmp/c8.out" 2> "$tmp/c8.err"
rc8=$?
_ok8=1; _8_msg=""
if [ "$rc8" -ne 0 ]; then
    _ok8=0; _8_msg="rc=$rc8"
elif grep -qF "is WITHDRAWN by a later line" "$tmp/c8.out"; then
    _ok8=0; _8_msg="read as a WITHDRAWAL (the 13:32 sentence named M3 + M2 beside M4): $(tr '\n' '|' < "$tmp/c8.out" | head_bytes 160)"
elif state_has withdrawn "$ST8"; then
    _ok8=0; _8_msg="wrote the state word \`withdrawn\` (it must not): state=$(fget "$ST8" | head_bytes 120)"
elif ! grep -qF "jhp-gate-$m4_8" "$tmp/ur-args" 2>/dev/null; then
    _ok8=0; _8_msg="did not reach the start arm (unit jhp-gate-$m4_8 not in ur-args): $(tr '\n' '|' < "$tmp/c8.out" | head_bytes 160)"
fi
if [ "$_ok8" -eq 1 ]; then
    t_ok "8: the 13:32 shape (J) -- the reference's exact sentence naming M3 (WITHDRAWN) and M2 beside the READY M4: NOT withdrawn (reaches the start arm; an ambiguous sentence does not withdraw)"
else
    t_fail "8: the 13:32 shape misbehaved -- $_8_msg -- on the red tree the \`watch\` verb does not exist"
fi

# --- 9: the genuine sole withdrawal (H) -------------------------------------
reset_stub
: > "$GH/digest.md"
LOG="$tmp/log9"
printf 'BATCH 9 READY at %s\n' "$m4_7" > "$LOG"
printf '%s is WITHDRAWN per the note 20261108-2237.\n' "$m4_7" >> "$LOG"
OUT9="$GH/out9"; ST9="$GH/state"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST9" --out "$OUT9" \
    < /dev/null > "$tmp/c9.out" 2> "$tmp/c9.err"
rc9=$?
_exp9="BATCH 9 READY at $m4_8 (log9:1) is WITHDRAWN by a later line: not gated"
_ok9=1; _9_msg=""
if [ "$rc9" -ne 0 ]; then
    _ok9=0; _9_msg="rc=$rc9"
elif ! grep -qF "$_exp9" "$tmp/c9.out"; then
    _ok9=0; _9_msg="withdraw say line not byte-exact (want \`$_exp9\`): $(tr '\n' '|' < "$tmp/c9.out" | head_bytes 160)"
elif [ "$(state_word "$ST9" "$m4_8")" != "withdrawn" ]; then
    _ok9=0; _9_msg="state word \`withdrawn\` not written for $m4_8: state=$(fget "$ST9" | head_bytes 120)"
elif ! grep -qF "BATCH 9 READY at $m4_8 (log9:1) read as WITHDRAWN by a later LOG sentence; not gated" "$GH/digest.md" 2>/dev/null \
      || ! grep -qF "append \"$m4_8 regate <time>\" to state" "$GH/digest.md" 2>/dev/null; then
    _ok9=0; _9_msg="behaviour-8 digest line not byte-exact (the regate hint names the READY sha8, and the TARGET is the --state file's basename, here state -- the reference's gate/state hard-code is CHANGED to the basename): $(fget "$GH/digest.md" | head_bytes 300)"
elif ! grep -qF "BATCH 9 READY at $m4_8 read as WITHDRAWN by a later LOG sentence; not gated; the state word is $m4_8 withdrawn" "$tmp/notified" 2>/dev/null; then
    _ok9=0; _9_msg="the notify stub was not called with the behaviour-9 withdraw TEXT: $(fget "$tmp/notified" | head_bytes 160)"
fi
if [ "$_ok9" -eq 1 ]; then
    t_ok "9: the genuine sole withdrawal (H) -- \`$m4_7 is WITHDRAWN per the note 20261108-2237.\` (the date is 8 digits, no a-f, not a token) -> the say line byte-exact, the state word \`withdrawn\`, the behaviour-8 digest line (incl. the regate hint naming the state file's basename), and the notify stub called (behaviour 9)"
else
    t_fail "9: the sole withdrawal misbehaved -- $_9_msg -- on the red tree the \`watch\` verb does not exist"
fi

# --- 10: the --dry half of 9 ------------------------------------------------
reset_stub
LOG="$tmp/log10"
printf 'BATCH 9 READY at %s\n' "$m4_7" > "$LOG"
printf '%s is WITHDRAWN per the note 20261108-2237.\n' "$m4_7" >> "$LOG"
OUT10="$GH/out10"; ST10="$GH/state-dry"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST10" --out "$OUT10" --dry \
    < /dev/null > "$tmp/c10.out" 2> "$tmp/c10.err"
rc10=$?
_exp10="BATCH 9 READY at $m4_8 (log10:1) is WITHDRAWN by a later line: not gated"
_ok10=1; _10_msg=""
if [ "$rc10" -ne 0 ]; then
    _ok10=0; _10_msg="rc=$rc10"
elif ! grep -qF "$_exp10" "$tmp/c10.out" "$OUT10"; then
    _ok10=0; _10_msg="--dry say line not byte-exact (want \`$_exp10\`): stdout=$(tr '\n' '|' < "$tmp/c10.out" | head_bytes 160) watch.out=$(fget "$OUT10" | head_bytes 160)"
elif [ -s "$ST10" ]; then
    _ok10=0; _10_msg="--dry tick wrote a state word (it must not): state=$(fget "$ST10" | head_bytes 120)"
elif [ -s "$tmp/notified" ]; then
    _ok10=0; _10_msg="--dry tick ran the notify stub (it must not): $(fget "$tmp/notified" | head_bytes 120)"
else
    t_ok "10: under --dry the same LOG -> the say line only: NO state word, NO digest line, NO notify stub call (the say line is the only dry output)"
fi
if [ "$_ok10" -ne 1 ]; then
    t_fail "10: the --dry withdrawal half misbehaved -- $_10_msg -- on the red tree the \`watch\` verb does not exist"
fi

# --- 11: the a-f / date / decimal / other-commit cases ----------------------
# (i) date: WITHDRAWN on 20261105 (8 digits, no a-f) -> withdrawn.
reset_stub
LOG="$tmp/log11i"
printf 'BATCH 11 READY at %s\n' "$m4_7" > "$LOG"
printf '%s is WITHDRAWN on 20261105.\n' "$m4_7" >> "$LOG"
OUT11i="$GH/out11i"; ST11i="$GH/state-w11i"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST11i" --out "$OUT11i" \
    < /dev/null > "$tmp/c11i.out" 2> "$tmp/c11i.err"
rc11i=$?
_i_ok=1; _i_msg=""
if [ "$rc11i" -ne 0 ]; then
    _i_ok=0; _i_msg="8i rc=$rc11i"
elif [ "$(state_word "$ST11i" "$m4_8")" != "withdrawn" ]; then
    _i_ok=0; _i_msg="8i the date (20261105, no a-f) did not stay withdrawn: state=$(fget "$ST11i" | head_bytes 120)"
fi
# (ii) decimal: WITHDRAWN, see 123456789012 (12 digits, no a-f) -> still withdrawn.
reset_stub
LOG="$tmp/log11ii"
printf 'BATCH 11 READY at %s\n' "$m4_7" > "$LOG"
printf '%s is WITHDRAWN, see 123456789012.\n' "$m4_7" >> "$LOG"
OUT11ii="$GH/out11ii"; ST11ii="$GH/state-w11ii"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST11ii" --out "$OUT11ii" \
    < /dev/null > "$tmp/c11ii.out" 2> "$tmp/c11ii.err"
rc11ii=$?
_ii_ok=1; _ii_msg=""
if [ "$rc11ii" -ne 0 ]; then
    _ii_ok=0; _ii_msg="8ii rc=$rc11ii"
elif [ "$(state_word "$ST11ii" "$m4_8")" != "withdrawn" ]; then
    _ii_ok=0; _ii_msg="8ii the 12-digit decimal (123456789012, no a-f) did not stay withdrawn: state=$(fget "$ST11ii" | head_bytes 120)"
fi
# (iii) other: WITHDRAWN (ref 9999999a) (8 hex WITH an a) -> NOT withdrawn.
reset_stub
LOG="$tmp/log11iii"
printf 'BATCH 11 READY at %s\n' "$m4_7" > "$LOG"
printf '%s is WITHDRAWN (ref 9999999a).\n' "$m4_7" >> "$LOG"
OUT11iii="$GH/out11iii"; ST11iii="$GH/state-w11iii"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST11iii" --out "$OUT11iii" \
    < /dev/null > "$tmp/c11iii.out" 2> "$tmp/c11iii.err"
rc11iii=$?
_iii_ok=1; _iii_msg=""
if [ "$rc11iii" -ne 0 ]; then
    _iii_ok=0; _iii_msg="8iii rc=$rc11iii"
elif state_has withdrawn "$ST11iii"; then
    _iii_ok=0; _iii_msg="8iii 9999999a (8 hex with an a) was treated as NO OTHER commit (the withdrawal fired): state=$(fget "$ST11iii" | head_bytes 120)"
fi
if [ "$_i_ok" -eq 1 ] && [ "$_ii_ok" -eq 1 ] && [ "$_iii_ok" -eq 1 ]; then
    t_ok "11: the a-f / date / decimal / other-commit cases -- (i) \`WITHDRAWN on 20261105\` -> withdrawn (the date has no a-f, not a token); (ii) \`WITHDRAWN, see 123456789012\` -> still withdrawn (12-digit decimal, no a-f, not a token); (iii) \`WITHDRAWN (ref 9999999a)\` -> NOT withdrawn (8 hex WITH an a: other)"
else
    t_fail "11: the a-f test misbehaved -- $( [ -n "$_i_msg" ] && printf '%s; ' "$_i_msg" ); $( [ -n "$_ii_msg" ] && printf '%s; ' "$_ii_msg" ); $( [ -n "$_iii_msg" ] && printf '%s' "$_iii_msg" ) -- on the red tree the \`watch\` verb does not exist"
fi

# --- 12: on-master (READY at M2, master's tip) ------------------------------
reset_stub
LOG="$tmp/log12"
printf 'BATCH 12 READY at %s\n' "$m2_7" > "$LOG"
OUT12="$GH/out12"; ST12="$GH/state-w12"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST12" --out "$OUT12" \
    < /dev/null > "$tmp/c12.out" 2> "$tmp/c12.err"
rc12=$?
_exp12="BATCH 12 READY at $m2_8: on master already ($m2_8)"
if [ "$rc12" -ne 0 ]; then
    t_fail "12: the on-master tick exited $rc12 (want 0) -- on the red tree the \`watch\` verb does not exist: $(tr '\n' ' ' < "$tmp/c12.err" | head_bytes 160)"
elif ! grep -qF "$_exp12" "$tmp/c12.out"; then
    t_fail "12: the behaviour-10 on-master say line is not byte-exact (want \`$_exp12\`): $(tr '\n' '|' < "$tmp/c12.out" | head_bytes 240)"
elif [ "$(state_word "$ST12" "$m2_8")" != "on-master" ]; then
    t_fail "12: the state word \`on-master\` was not written for $m2_8: state=$(fget "$ST12" | head_bytes 120)"
else
    t_ok "12: on-master -- READY at $m2_7 (master's tip) -> the behaviour-10 say line byte-exact, the state word \`on-master\`, exit 0"
fi

# --- 13: not-ff (the side commit) -------------------------------------------
reset_stub
LOG="$tmp/log13"
printf 'BATCH 13 READY at %s\n' "$side_7" > "$LOG"
OUT13="$GH/out13"; ST13="$GH/state-w13"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST13" --out "$OUT13" \
    < /dev/null > "$tmp/c13.out" 2> "$tmp/c13.err"
rc13=$?
_exp13="BATCH 13 READY at $side_8: NOT a fast-forward of master $m2_8"
_redtext="RED (not gated): BATCH 13 READY at $side_8 is not a fast-forward of master $m2_8. Rebase or merge master into the team branch, then write a new READY line."
_ok13=1; _13_msg=""
if [ "$rc13" -ne 0 ]; then
    _ok13=0; _13_msg="rc=$rc13"
elif ! grep -qF "$_exp13" "$tmp/c13.out"; then
    _ok13=0; _13_msg="say not byte-exact (want \`$_exp13\`): $(tr '\n' '|' < "$tmp/c13.out" | head_bytes 160)"
elif [ "$(state_word "$ST13" "$side_8")" != "not-ff" ]; then
    _ok13=0; _13_msg="state word \`not-ff\` not written for $side_8: state=$(fget "$ST13" | head_bytes 120)"
elif ! grep -qF "$_redtext" "$tmp/notified" 2>/dev/null; then
    _ok13=0; _13_msg="the notify stub was not called with the reference's RED TEXT byte-exact (behaviour 9): want \`$_redtext\`, got: $(fget "$tmp/notified" | head_bytes 300)"
fi
if [ "$_ok13" -eq 1 ]; then
    t_ok "13: not-ff -- READY at the side commit $side_7 (on master's side, not an ancestor of master M2, not a descendant) -> the say line, the state word \`not-ff\`, the notify stub called with the reference's RED TEXT byte-exact, exit 0"
else
    t_fail "13: the not-ff tick misbehaved -- $_13_msg -- on the red tree the \`watch\` verb does not exist"
fi

# --- 14: no origin/master ref (push2.git) ----------------------------------
reset_stub
LOG="$tmp/log14"
printf 'BATCH 14 READY at %s\n' "$m5_7" > "$LOG"
OUT14="$GH/out14"; ST14="$GH/state-w14"
"$HBIN" watch gate --config "$tmp/cfg-push2.json" --log "$LOG" --state "$ST14" --out "$OUT14" \
    < /dev/null > "$tmp/c14.out" 2> "$tmp/c14.err"
rc14=$?
if [ "$rc14" -ne 1 ]; then
    t_fail "14: the no-origin/master tick (push2.git without the ref) exited $rc14 (want 1) -- on the red tree the \`watch\` verb does not exist: $(tr '\n' ' ' < "$tmp/c14.err" | head_bytes 160)"
elif ! grep -qF "no origin/master in push.git" "$tmp/c14.out"; then
    t_fail "14: the no-origin/master say line is not byte-exact (want \`no origin/master in push.git\`): $(tr '\n' '|' < "$tmp/c14.out" | head_bytes 200)"
else
    t_ok "14: no origin/master in push.git (push2.git without the ref) -> the say line byte-exact, exit 1 (the reference's only exit-1: a service that cannot name master is broken, not waiting)"
fi

# --- 15: the busy filter (behaviour 11, DD5) --------------------------------
# Each letter sets the unit-list stub's output; READY at M5 (happy path);
# busy -> exit 0 with the say line naming only the matching units; not busy
# -> the tick proceeds to the start arm.
_busy_check() { # $1=label $2=units-content $3=busy(1/0) [$4=expected-unit-basename]
    reset_stub
    printf '%s\n' "$2" > "$tmp/units"
    LOG="$tmp/log15_$1"
    printf 'BATCH 15 READY at %s\n' "$m5_7" > "$LOG"
    OUT15="$GH/out15_$1"; ST15="$GH/state-w15_$1"
    "$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST15" --out "$OUT15" \
        < /dev/null > "$tmp/c15_$1.out" 2> "$tmp/c15_$1.err"
    rc=$?
    if [ "$3" = 1 ]; then
        [ "$rc" -eq 0 ] || { printf '11%s rc=%s (want 0, busy)\n' "$1" "$rc"; return 1; }
        grep -qF "waiting, a gate runs" "$OUT15" || { printf '11%s no busy say\n' "$1"; return 1; }
        if [ -n "$4" ]; then
            grep -qF "$4" "$OUT15" || { printf '11%s say does not name %s\n' "$1" "$4"; return 1; }
            # M1178: the say line byte for byte, as the reference prints it
            # (`tr '\n' ' '` leaves a space after EVERY unit, the last too),
            # since the shadow compares the two say lines exactly.
            grep -qF "BATCH 15 READY at $m5_8: waiting, a gate runs ($4 )" "$OUT15" \
                || { printf '11%s say is not the reference%s bytes (want "(%s )")\n' "$1" "'s" "$4"; return 1; }
        fi
        # the watcher's own unit must NOT be named (the 01:58 self-match's teeth).
        if grep -qF "gate-watch" "$OUT15"; then
            printf '11%s say names the watcher%s own unit (01:58 self-match)\n' "$1"'s'; return 1
        fi
    else
        [ "$rc" -eq 0 ] || { printf '11%s rc=%s (want 0, not busy)\n' "$1" "$rc"; return 1; }
        grep -qF "jhp-gate-$m5_8" "$tmp/ur-args" 2>/dev/null || { printf '11%s not-busy did not proceed to the start\n' "$1"; return 1; }
    fi
    return 0
}
_15_err=""
_busy_check a "jhp-gate-watch.service
jhp-gate-$m5_8.service" 1 "jhp-gate-$m5_8.service" || _15_err="$_15_err a"
_busy_check b "jhp-gate-watch.service" 0 || _15_err="$_15_err b"
_busy_check c "jhp-ci-$m5_8.service" 1 "jhp-ci-$m5_8.service" || _15_err="$_15_err c"
_busy_check d "jhd-gate-$m5_8.service" 1 "jhd-gate-$m5_8.service" || _15_err="$_15_err d"
_busy_check e "jhp-gate-watch.timer
jhd-gate-watch.service" 0 || _15_err="$_15_err e"
if [ -n "$_15_err" ]; then
    t_fail "15: the busy filter misbehaved -- letters ($_15_err) failed -- on the red tree the \`watch\` verb does not exist"
else
    t_ok "15: the busy filter (DD5) -- (a) jhp-gate-watch.service + jhp-gate-<M5-8>.service -> busy, the say names ONLY the M5 unit (the watcher's own unit is filtered out, the 01:58 self-match's teeth); (b) only jhp-gate-watch.service -> NOT busy (proceeds); (c) jhp-ci-<M5-8>.service -> busy; (d) jhd-gate-<M5-8>.service (the OTHER drive's prefix) -> busy (DD5's teeth); (e) jhp-gate-watch.timer + jhd-gate-watch.service (both watches' own units) -> NOT busy"
fi

# --- 16: the team-smoke wait (behaviour 12) --------------------------------
# A REAL background process whose argv contains `tests/smoke/` (a script at
# $tmp/tests/smoke/hold.sh) and whose cwd is $DIR/team. The tick waits (the
# say names the pid); after it is killed the next tick proceeds. The process
# is killed at the cell's end and in the trap (no leak).
reset_stub
mkdir -p "$DIR/team" "$tmp/tests/smoke" 2>/dev/null
cat > "$tmp/tests/smoke/hold.sh" <<EOF
#!/bin/sh
cd "$DIR/team"
sleep 25
EOF
chmod 755 "$tmp/tests/smoke/hold.sh"
"$tmp/tests/smoke/hold.sh" &
GC_PID=$!
# Wait for the process to appear (its /proc entry is readable).
_i=0
while [ "$_i" -lt 50 ] && [ ! -d "/proc/$GC_PID" ]; do
    _i=$((_i + 1))
done
if ! kill -0 "$GC_PID" 2>/dev/null; then
    GC_PID=""
    t_fail "16: the team-smoke background process did not start"
fi
if [ -n "$GC_PID" ]; then
    LOG="$tmp/log16"
    printf 'BATCH 16 READY at %s\n' "$m5_7" > "$LOG"
    OUT16="$GH/out16"; ST16="$GH/state-w16"
    "$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST16" --out "$OUT16" \
        < /dev/null > "$tmp/c16a.out" 2> "$tmp/c16a.err"
    rc16a=$?
    _16_ok=1
    if [ "$rc16a" -ne 0 ]; then
        _16_ok=0
    elif ! grep -qF "waiting, a team smoke runs (pid $GC_PID)" "$tmp/c16a.out"; then
        _16_ok=0
    fi
    # Kill it; the next tick proceeds.
    kill "$GC_PID" 2>/dev/null
    wait "$GC_PID" 2>/dev/null
    GC_PID=""
    if [ "$_16_ok" -eq 1 ]; then
        OUT16b="$GH/out16b"; ST16b="$GH/state-w16b"
        "$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST16b" --out "$OUT16b" \
            < /dev/null > "$tmp/c16b.out" 2> "$tmp/c16b.err"
        rc16b=$?
        if [ "$rc16b" -ne 0 ] || ! grep -qF "jhp-gate-$m5_8" "$tmp/ur-args" 2>/dev/null; then
            _16_ok=0
        fi
    fi
    if [ "$_16_ok" -eq 1 ]; then
        t_ok "16: the team-smoke wait -- a REAL background process (argv contains tests/smoke/, cwd is $DIR/team) -> the tick waits (the say names its pid); after it is killed the next tick proceeds (reaches the start arm)"
    else
        t_fail "16: the team-smoke wait misbehaved -- on the red tree the \`watch\` verb does not exist: $(tr '\n' ' ' < "$tmp/c16a.err" | head_bytes 160)"
    fi
fi

# --- 17: the start (behaviour 13) -------------------------------------------
reset_stub
LOG="$tmp/log17"
printf 'BATCH 17 READY at %s\n' "$m5_7" > "$LOG"
OUT17="$GH/out17"; ST17="$GH/state-w17"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST17" --out "$OUT17" \
    < /dev/null > "$tmp/c17.out" 2> "$tmp/c17.err"
rc17=$?
_exp17="BATCH 17 READY at $m5_8 (log17:1): starting jhp-gate-$m5_8"
_ok17=1; _17_msg=""
if [ "$rc17" -ne 0 ]; then
    _ok17=0; _17_msg="rc=$rc17"
elif [ "$(state_word "$ST17" "$m5_8")" != "started" ]; then
    _ok17=0; _17_msg="state word \`started\` not written for $m5_8: state=$(fget "$ST17" | head_bytes 120)"
elif ! grep -qF "$_exp17" "$tmp/c17.out" "$OUT17"; then
    _ok17=0; _17_msg="the \`starting\` say line not byte-exact (want \`$_exp17\`): $(tr '\n' '|' < "$tmp/c17.out" | head_bytes 160)"
elif ! grep -qF -- "--unit=jhp-gate-$m5_8" "$tmp/ur-args" 2>/dev/null; then
    _ok17=0; _17_msg="the unit is not the \`--unit=\` option (--unit=jhp-gate-$m5_8 not in ur-args; the unfixed tree passes it as a bare positional): $(tr '\n' '|' < "$tmp/ur-args" | head_bytes 120)"
elif ! grep -qxF -- "-p" "$tmp/ur-args" 2>/dev/null; then
    _ok17=0; _17_msg="the description is not the -p PROPERTY (no standalone -p line in ur-args; without it Description=... is a bare positional systemd-run would exec): $(tr '\n' '|' < "$tmp/ur-args" | head_bytes 120)"
elif ! grep -qxF "Description=jichi drive gate on $m5_8 (BATCH 17)" "$tmp/ur-args" 2>/dev/null; then
    _ok17=0; _17_msg="the description is not the -p property line \`Description=jichi drive gate on $m5_8 (BATCH 17)\`: $(tr '\n' '|' < "$tmp/ur-args" | head_bytes 120)"
elif ! grep -qxF "$UHOME/gate.sh" "$tmp/ur-args" 2>/dev/null; then
    _ok17=0; _17_msg="the exec's gateRun word is not its own line (\`$UHOME/gate.sh\`): $(tr '\n' '|' < "$tmp/ur-args" | head_bytes 120)"
elif ! grep -qxF "$m5_8" "$tmp/ur-args" 2>/dev/null; then
    _ok17=0; _17_msg="the exec's sha8 word is not its own line (\`$m5_8\`): $(tr '\n' '|' < "$tmp/ur-args" | head_bytes 120)"
elif ! grep -qxF "17" "$tmp/ur-args" 2>/dev/null; then
    _ok17=0; _17_msg="the exec's batch word is not its own line (\`17\`): $(tr '\n' '|' < "$tmp/ur-args" | head_bytes 120)"
elif grep -qF "$UHOME/gate.sh $m5_8 17" "$tmp/ur-args" 2>/dev/null; then
    _ok17=0; _17_msg="the exec is still ONE space-joined word (\`<gateRun> $m5_8 17\`) instead of separate words: $(tr '\n' '|' < "$tmp/ur-args" | head_bytes 120)"
elif ! grep -qF "GATE-UNIT-RAN: marker for this driver" "$OUT17"; then
    _ok17=0; _17_msg="the stub's marker line not in watch.out (the child's stdout is redirected there): $(fget "$OUT17" | head_bytes 160)"
fi
if [ "$_ok17" -eq 1 ]; then
    t_ok "17: the start -- READY at $m5_7 (the happy path, master at $m2_7) -> the state word \`started\`, the say line byte-exact, and the unit-run stub's argv in the reference's token order (M1188): the \`--unit=jhp-gate-$m5_8\` option, the -p property line \`Description=jichi drive gate on $m5_8 (BATCH 17)\`, and the exec as SEPARATE words (\`<gateRun>\`, \`$m5_8\`, \`17\` -- NOT one space-joined word), plus the stub's marker line in watch.out"
else
    t_fail "17: the start tick misbehaved -- $_17_msg -- on the red tree the \`watch\` verb does not exist"
fi

# --- 18: unit-run stub rc 7 -> the KEPT half --------------------------------
reset_stub
printf '7\n' > "$tmp/urrc"
LOG="$tmp/log18"
printf 'BATCH 18 READY at %s\n' "$m5_7" > "$LOG"
OUT18="$GH/out18"; ST18="$GH/state-w18"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST18" --out "$OUT18" \
    < /dev/null > "$tmp/c18.out" 2> "$tmp/c18.err"
rc18=$?
: > "$tmp/urrc"
_ok18=1; _18_msg=""
if [ "$rc18" -ne 1 ]; then
    _ok18=0; _18_msg="rc=$rc18 (want 1)"
elif ! grep -qF "systemd-run failed (rc=7)" "$tmp/c18.out" "$OUT18"; then
    _ok18=0; _18_msg="the \`systemd-run failed (rc=7)\` say line not byte-exact: $(tr '\n' '|' < "$tmp/c18.out" | head_bytes 160)"
elif [ "$(state_word "$ST18" "$m5_8")" != "started" ]; then
    _ok18=0; _18_msg="after the unit-run rc=7 the state word \`started\` is NOT PRESENT (behaviour 13's KEPT half): state=$(fget "$ST18" | head_bytes 120)"
fi
if [ "$_ok18" -eq 1 ]; then
    t_ok "18: unit-run stub rc 7 -> say \`systemd-run failed (rc=7)\`, exit 1, the state word \`started\` STILL PRESENT (behaviour 13's KEPT half: a start that died leaves \`started\` in the state, and behaviour 4's is-active check re-gates it next tick)"
else
    t_fail "18: the unit-run rc=7 half misbehaved -- $_18_msg -- on the red tree the \`watch\` verb does not exist"
fi

# --- 19: --dry -> the DRY say line, no stub, no state ------------------------
reset_stub
LOG="$tmp/log19"
printf 'BATCH 19 READY at %s\n' "$m5_7" > "$LOG"
OUT19="$GH/out19"; ST19="$GH/state-w19"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST19" --out "$OUT19" --dry \
    < /dev/null > "$tmp/c19.out" 2> "$tmp/c19.err"
rc19=$?
_exp19="DRY: would start jhp-gate-$m5_8 (gate-run.sh $m5_8) for BATCH 19 (log19:1)"
_ok19=1; _19_msg=""
if [ "$rc19" -ne 0 ]; then
    _ok19=0; _19_msg="rc=$rc19"
elif ! grep -qF "$_exp19" "$tmp/c19.out" "$OUT19"; then
    _ok19=0; _19_msg="the --dry DRY say line not byte-exact (want \`$_exp19\`): $(tr '\n' '|' < "$tmp/c19.out" | head_bytes 160)"
elif [ -s "$tmp/ur-args" ]; then
    _ok19=0; _19_msg="the --dry tick called the unit-run stub (it must not): ur-args=$(fget "$tmp/ur-args" | head_bytes 120)"
elif [ -s "$ST19" ]; then
    _ok19=0; _19_msg="the --dry tick wrote a state word (it must not): state=$(fget "$ST19" | head_bytes 120)"
fi
if [ "$_ok19" -eq 1 ]; then
    t_ok "19: --dry -> the DRY say line byte-exact, the stub NOT called, no state word (the say line is the only dry output)"
else
    t_fail "19: the --dry start half misbehaved -- $_19_msg -- on the red tree the \`watch\` verb does not exist"
fi

# --- 20: the units + the usage ----------------------------------------------
od20="$tmp/units20"; mkdir -p "$od20"
"$HBIN" watch units --config "$tmp/cfg.json" -o "$od20" \
    < /dev/null > "$tmp/c20.out" 2> "$tmp/c20.err"
rc20=$?
svc20="$od20/jhp-gate-watch.service"; tmr20="$od20/jhp-gate-watch.timer"
# The absent-gateOnCalendar refusal (a second config without the key).
"$HBIN" watch units --config "$tmp/cfg-nocal.json" \
    < /dev/null > "$tmp/c20b.out" 2> "$tmp/c20b.err"
rc20b=$?
# The usage: bare `watch` (names `gate`), `watch frob`, and no host block.
"$HBIN" watch < /dev/null > "$tmp/c20c.out" 2> "$tmp/c20c.err"
rc20c=$?
"$HBIN" watch frob < /dev/null > "$tmp/c20d.out" 2> "$tmp/c20d.err"
rc20d=$?
"$HBIN" --config "$tmp/cfg-nohost.json" watch gate < /dev/null > "$tmp/c20e.out" 2> "$tmp/c20e.err"
rc20e=$?
_ok20=1; _20_msg=""
if [ "$rc20" -ne 0 ]; then
    _ok20=0; _20_msg="watch units rc=$rc20 (want 0) -- on the red tree the verb does not exist: $(tr '\n' ' ' < "$tmp/c20.err" | head_bytes 160)"
elif [ ! -f "$tmr20" ] || [ ! -f "$svc20" ]; then
    _ok20=0; _20_msg="watch units did not write jhp-gate-watch.service + jhp-gate-watch.timer: $(ls "$od20" 2>/dev/null | tr '\n' ' ')"
elif ! grep -qF "OnCalendar=$OC" "$tmr20"; then
    _ok20=0; _20_msg="the timer does not carry OnCalendar=$OC"
elif ! grep -qF "Persistent=true" "$tmr20"; then
    _ok20=0; _20_msg="the timer does not carry Persistent=true"
elif ! grep -qF "AccuracySec=30s" "$tmr20"; then
    _ok20=0; _20_msg="the timer does not carry AccuracySec=30s"
elif ! grep -qF "watch gate --config" "$svc20" || ! grep -qF "$tmp/cfg.json" "$svc20"; then
    _ok20=0; _20_msg="the service's ExecStart does not name the watch command with the config's argv0 path: $(grep -F ExecStart "$svc20" 2>/dev/null | head_bytes 200)"
elif [ "$rc20b" -ne 2 ] || ! grep -qiE "gateOnCalendar" "$tmp/c20b.err"; then
    _ok20=0; _20_msg="the absent-gateOnCalendar refusal did not exit 2 naming gateOnCalendar: rc=$rc20b $(tr '\n' ' ' < "$tmp/c20b.err" | head_bytes 160)"
elif [ "$rc20c" -ne 2 ] || ! grep -qiE "gate" "$tmp/c20c.err"; then
    _ok20=0; _20_msg="bare \`watch\` did not exit 2 naming \`gate\`: rc=$rc20c $(tr '\n' ' ' < "$tmp/c20c.err" | head_bytes 160)"
elif [ "$rc20d" -ne 2 ]; then
    _ok20=0; _20_msg="\`watch frob\` did not exit 2: rc=$rc20d"
elif [ "$rc20e" -ne 2 ] || ! grep -qiE "no host block" "$tmp/c20e.err"; then
    _ok20=0; _20_msg="\`watch gate\` with no host block did not exit 2 naming \`no host block\`: rc=$rc20e $(tr '\n' ' ' < "$tmp/c20e.err" | head_bytes 160)"
fi
if [ "$_ok20" -eq 1 ]; then
    t_ok "20: watch units (timer OnCalendar=$OC / Persistent=true / AccuracySec=30s, the service's ExecStart names the watch command with the config's argv0 path, an absent gateOnCalendar refuses exit 2 naming it) + the usage (bare \`watch\` exits 2 naming \`gate\`, \`watch frob\` exits 2, \`watch gate\` with no host block exits 2 naming \`no host block\`)"
else
    t_fail "20: the units/usage half misbehaved -- $_20_msg (on the red tree every \`watch\` variant is \`unknown command 'watch'\`, exit 2; the green shape is what the cell asserts, and it will only pass once wave B builds \`watch units\`)"
fi

# --- 21: the --log default (M1156) -------------------------------------------
# A config whose dir is this cell's OWN fixture dir holding LOG.md; the tick
# runs with NO --log. The READY token 0deadbe is 7 hex (it parses) but not an
# object in push.git, so the tick takes the wait-for-remote arm (no unit
# start, no shims needed). (a) the tick must read <dir>/LOG.md -- rc 0, NOT
# the unfixed refusal (rc 1, no say line); (b) the say line must NAME the
# log's leaf (LOG.md:1); (c) an explicit --log <path-with-slashes> is
# honoured byte-for-byte and names THAT path's leaf (LOG.md:1 again).
reset_stub
D21="$tmp/dir21"
GH21="$D21/gate"
if ! mkdir -p "$D21" "$GH21"; then
    t_fail "21: the cell's fixture dirs could not be made"
else
    printf 'BATCH 99 READY at 0deadbe\n' > "$D21/LOG.md"
    cat > "$tmp/cfg21.json" <<EOF
{"dir":"$D21",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"drive":{"branchPrefix":"$PFX","gitName":"Harness Smoke","gitEmail":"harness-smoke@example.com"},
"host":{"pushHome":"$UHOME","gateHome":"$GH21","gateOnCalendar":"$OC","gateRun":"$UHOME/gate.sh",
"unitState":"$UHOME/unit-state.sh","unitList":"$UHOME/unit-list.sh","unitRun":"$UHOME/unit-run.sh","notifyCommand":"$UHOME/notify.sh"}}
EOF
    OUT21="$GH21/out21"; ST21="$GH21/st21"
    _exp21="BATCH 99 READY at 0deadbe (LOG.md:1): not on the remote's team branch yet; waiting for the push timer"
    "$HBIN" watch gate --config "$tmp/cfg21.json" --state "$ST21" --out "$OUT21" \
        < /dev/null > "$tmp/c21.out" 2> "$tmp/c21.err"
    rc21=$?
    _ok21=1; _21_msg=""
    if [ "$rc21" -ne 0 ]; then
        _ok21=0; _21_msg="rc=$rc21 (want 0; the unfixed tick refuses rc 1 for lacking --log with no say line) err=$(tr '\n' ' ' < "$tmp/c21.err" | head_bytes 160) out=$(tr '\n' '|' < "$tmp/c21.out" | head_bytes 160)"
    elif ! grep -qF "$_exp21" "$tmp/c21.out"; then
        _ok21=0; _21_msg="the no --log wait-for-remote say line does not name the leaf (want \`$_exp21\`): $(tr '\n' '|' < "$tmp/c21.out" | head_bytes 240)"
    else
        # (c) the explicit --log <path-with-slashes> is honoured and names
        # THAT path's leaf.
        OUT21b="$GH21/out21b"; ST21b="$GH21/st21b"
        "$HBIN" watch gate --config "$tmp/cfg21.json" --log "$D21/LOG.md" --state "$ST21b" --out "$OUT21b" \
            < /dev/null > "$tmp/c21b.out" 2> "$tmp/c21b.err"
        rc21b=$?
        if [ "$rc21b" -ne 0 ]; then
            _ok21=0; _21_msg="the explicit --log tick rc=$rc21b (want 0): $(tr '\n' ' ' < "$tmp/c21b.err" | head_bytes 160)"
        elif ! grep -qF "$_exp21" "$tmp/c21b.out"; then
            _ok21=0; _21_msg="the explicit --log say line does not name that path's leaf (want \`$_exp21\`): $(tr '\n' '|' < "$tmp/c21b.out" | head_bytes 240)"
        fi
    fi
    if [ "$_ok21" -eq 1 ]; then
        t_ok "21: the --log default (M1156) -- a tick with NO --log on a config whose dir holds LOG.md reads <dir>/LOG.md (rc 0, not the unfixed refusal for lacking --log) and the wait-for-remote say line names the leaf (LOG.md:1); an explicit --log <dir>/LOG.md is honoured and names that path's leaf too"
    else
        t_fail "21: the --log default misbehaved -- $_21_msg"
    fi
fi

# --- 21b (the 22nd cell): the empty --log "" half (M1156, the H9a1 WEAK's gap) -
# Same fixture shape as cell 21 (dir21 holds LOG.md; the READY token 0deadbe is
# 7 hex but not an object in push.git, so the tick takes the wait-for-remote
# arm). Here the tick is run with --log "" EXPLICITLY -- an empty string, not an
# absent flag. The fixed branch is `log == NULL || log[0] == '\0'`
# (jc_hp_watchgate.c:1094): the ABSENT (NULL) half is pinned by cell 21, but the
# EMPTY half was pinned by NO cell (the H9a1 independent review's WEAK row,
# LOG 16:25). Its teeth mutant drops only the `|| log[0] == '\0'` half; then an
# empty --log is treated as a real (empty) path, wgate_read_file returns NULL
# (nothing opens), and the tick goes QUIET (rc 0, NO say line) where the fixed
# tree prints the wait-for-remote line. The leaf is named from the DEFAULT path
# <dir21>/LOG.md because the empty string takes the default arm -- so the say
# line is byte-identical to cell 21's.
if [ ! -d "$D21" ]; then
    t_fail "22: the cell 21 fixture dir21 is absent (cell 21 did not set it up)"
else
    OUT21c="$GH21/out21c"; ST21c="$GH21/st21c"
    _exp21c="BATCH 99 READY at 0deadbe (LOG.md:1): not on the remote's team branch yet; waiting for the push timer"
    "$HBIN" watch gate --config "$tmp/cfg21.json" --log "" --state "$ST21c" --out "$OUT21c" \
        < /dev/null > "$tmp/c21c.out" 2> "$tmp/c21c.err"
    rc21c=$?
    _ok21c=1; _21c_msg=""
    if [ "$rc21c" -ne 0 ]; then
        _ok21c=0; _21c_msg="rc=$rc21c (want 0): $(tr '\n' ' ' < "$tmp/c21c.err" | head_bytes 160)"
    elif ! grep -qF "$_exp21c" "$tmp/c21c.out"; then
        _ok21c=0; _21c_msg="the empty --log tick did not take the wait-for-remote arm naming the DEFAULT leaf (LOG.md:1) -- want \`$_exp21c\`; this is the mutant (the \`|| log[0] == '\0'\` half dropped) going QUIET: $(tr '\n' '|' < "$tmp/c21c.out" | head_bytes 240)"
    fi
    if [ "$_ok21c" -eq 1 ]; then
        t_ok "22: the empty --log \"\" half (M1156, cell 21b -- the H9a1 WEAK's gap) -- a tick with --log \"\" EXPLICITLY (an empty string, not an absent flag) takes the default arm, reads <dir21>/LOG.md, and prints the wait-for-remote line byte-exact naming the DEFAULT leaf (LOG.md:1), rc 0 -- pinning the \`|| log[0] == '\0'\` half the H9a1 independent review found unpinned"
    else
        t_fail "22: the empty --log \"\" half (cell 21b) misbehaved -- $_21c_msg"
    fi
fi

# --- 23 (M1178): a WITHDRAWN line, then a line naming the READY commit ------
# Two LINES, not one sentence. Until M1178 the tick passed jc_gate_withdrawn a
# pointer INTO the LOG buffer and no length, so the call read to the end of
# the LOG: the newline is no sentence boundary, the two lines became one
# sentence that says WITHDRAWN and names M4, and the batch read as withdrawn.
# Red on the pre-M1178 binary (the withdraw say line); green: the start arm.
reset_stub
LOG="$tmp/log23"
printf 'BATCH 23 READY at %s\n' "$m4_7" > "$LOG"
printf 'the earlier plan is WITHDRAWN\n' >> "$LOG"
printf '%s stays the batch to gate\n' "$m4_7" >> "$LOG"
OUT23="$GH/out23"; ST23="$GH/st23"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST23" --out "$OUT23" \
    < /dev/null > "$tmp/c23.out" 2> "$tmp/c23.err"
rc23=$?
_ok23=1; _23_msg=""
if [ "$rc23" -ne 0 ]; then
    _ok23=0; _23_msg="rc=$rc23"
elif grep -qF "is WITHDRAWN by a later line" "$tmp/c23.out"; then
    _ok23=0; _23_msg="read as a WITHDRAWAL across two lines: $(tr '\n' '|' < "$tmp/c23.out" | head_bytes 160)"
elif state_has withdrawn "$ST23"; then
    _ok23=0; _23_msg="wrote the state word \`withdrawn\`: state=$(fget "$ST23" | head_bytes 120)"
elif ! grep -qF "jhp-gate-$m4_8" "$tmp/ur-args" 2>/dev/null; then
    _ok23=0; _23_msg="did not reach the start arm: $(tr '\n' '|' < "$tmp/c23.out" | head_bytes 160)"
fi
if [ "$_ok23" -eq 1 ]; then
    t_ok "23: a line saying WITHDRAWN, then a line naming the READY M4: two lines, not one sentence -> NOT withdrawn (the start arm) (M1178)"
else
    t_fail "23: the two-line shape misbehaved -- $_23_msg"
fi

# --- 24 (M1178): a withdrawal followed by more than 64 KiB of LOG -----------
# The same fault from the other side: the call's length was the REST of the
# LOG, and a "line" of 64 KiB or more is not a withdrawal, so a genuine
# withdrawal with that much LOG after it was never seen and the batch was
# gated. The filler names no commit token (no 7+ run of hex with a-f).
reset_stub
LOG="$tmp/log24"
printf 'BATCH 24 READY at %s\n' "$m4_7" > "$LOG"
printf '%s is WITHDRAWN.\n' "$m4_7" >> "$LOG"
awk 'BEGIN { for (i = 1; i <= 2000; i++) printf "filler line %d of the later log, no commit named here\n", i }' > "$tmp/fill24"
_sz24=$(wc -c < "$tmp/fill24" | tr -d ' ')
cat "$tmp/fill24" >> "$LOG"
OUT24="$GH/out24"; ST24="$GH/st24"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST24" --out "$OUT24" \
    < /dev/null > "$tmp/c24.out" 2> "$tmp/c24.err"
rc24=$?
_exp24="BATCH 24 READY at $m4_8 (log24:1) is WITHDRAWN by a later line: not gated"
_ok24=1; _24_msg=""
if [ "$_sz24" -le 65536 ]; then
    _ok24=0; _24_msg="the filler after the withdrawal is $_sz24 B, not more than 64 KiB: the cell would prove nothing"
elif [ "$rc24" -ne 0 ]; then
    _ok24=0; _24_msg="rc=$rc24"
elif ! grep -qF "$_exp24" "$tmp/c24.out"; then
    _ok24=0; _24_msg="the withdrawal was not seen (want \`$_exp24\`): $(tr '\n' '|' < "$tmp/c24.out" | head_bytes 160)"
elif [ "$(state_word "$ST24" "$m4_8")" != "withdrawn" ]; then
    _ok24=0; _24_msg="state word \`withdrawn\` not written: state=$(fget "$ST24" | head_bytes 120)"
fi
if [ "$_ok24" -eq 1 ]; then
    t_ok "24: \`<M4-7> is WITHDRAWN.\` followed by $_sz24 B of later LOG -> withdrawn (the say line byte-exact, the state word) (M1178)"
else
    t_fail "24: the long-tail withdrawal misbehaved -- $_24_msg"
fi

# --- 25 (M1178): a LOG of more than 1 MiB is still read ----------------------
# Until M1178 the LOG read refused a file of 1 MiB or more, and the tick took
# it for an absent LOG: the quiet tick, every time, so no READY was gated ever
# again. The LOG is append-only; the live drive's had reached 474,487 B.
reset_stub
LOG="$tmp/log25"
awk 'BEGIN { for (i = 1; i <= 25000; i++) printf "filler line %d of an older log, no commit named here\n", i }' > "$LOG"
printf 'BATCH 25 READY at %s\n' "$m4_7" >> "$LOG"
_sz25=$(wc -c < "$LOG" | tr -d ' ')
OUT25="$GH/out25"; ST25="$GH/st25"
"$HBIN" watch gate --config "$tmp/cfg.json" --log "$LOG" --state "$ST25" --out "$OUT25" \
    < /dev/null > "$tmp/c25.out" 2> "$tmp/c25.err"
rc25=$?
_ok25=1; _25_msg=""
if [ "$_sz25" -le 1048576 ]; then
    _ok25=0; _25_msg="the fixture is $_sz25 B, not more than 1 MiB: the cell would prove nothing"
elif [ "$rc25" -ne 0 ]; then
    _ok25=0; _25_msg="rc=$rc25"
elif ! grep -qF "jhp-gate-$m4_8" "$tmp/ur-args" 2>/dev/null; then
    _ok25=0; _25_msg="did not reach the start arm (the quiet tick of an unread LOG?): out=$(tr '\n' '|' < "$tmp/c25.out" | head_bytes 160)"
fi
if [ "$_ok25" -eq 1 ]; then
    t_ok "25: a LOG of $_sz25 B (more than 1 MiB) with the READY at its end -> read, and the start arm runs (M1178)"
else
    t_fail "25: the large LOG misbehaved -- $_25_msg"
fi

t_done
