#!/bin/sh
# smoke: `jichi-harness push tick --config <cfg>` -- one host-push tick under
# the lock, with paging and the consecutive-failure state (M1147, batch H7;
# the C89 port of the 2026-10-27 hardening-host drive's push-tick.sh + the
# push.sh lock, behaviour B2). A tick takes <pushHome>/push.lock (a lock, not
# a cap: a second tick WAITS), runs the team push (the built
# `jc_harness_mirror_push_run` machinery) THEN the records push (B1), keeps
# <pushHome>/push-tick.state (a one-line consecutive-failure count,
# non-numeric resets to 0), appends TICK ok / TICK FAIL / PAGE / RECOVERED
# lines to <pushHome>/push-tick.out, and exits with the push's rc (the
# systemd oneshot passes it through). On the FIRST failure and every 6th
# consecutive one it writes the PAGE line
# `jichi drive: the host push failed ($fails tick(s) in a row); see
# <pushHome>/push.log` and runs `host.pageCommand` with that message; a
# config without pageCommand writes the PAGE line and runs nothing (H7's
# stop).
#
# M1147 (H7). Design note (the contract, behaviour list B2):
#   docs/internal/drive/2026-11-05-harness-port/notes/M1147.md
# Ported reference (read line by line; this driver holds B2):
#   docs/internal/drive/2026-10-27-hardening-host/push-tick.sh
#   docs/internal/drive/2026-10-27-hardening-host/push.sh        (the lock)
#
# M1211 (H7 follow-up). The push tick's push.git refresh (card [104], SV:1355):
#   docs/internal/drive/2026-11-05-harness-port/notes/M1211.md
#
# THE RED THIS IS HELD BY. On the tree before this batch the top-level verb
# `push` does not exist in jichi-harness: every check that runs a push
# subcommand gets `unknown command 'push'` and exit 2. All eight cells run
# the tick and are RED for exactly that reason (rc 2 instead of 0/1, and
# every push.log / push-tick.out line they assert is absent).
#
# WHAT IS CHECKED -- 14 cells, one t_ok/t_fail each:
#   1. two ticks started 1 s apart are serialised by the lock: the second
#      WAITS, it does not fail and does not interleave. The first tick's
#      records push is slow (a 32 MiB blob: the key-scan cat-file loop over
#      it + the push, ~2 s), so the second tick starts while the first still
#      holds the lock. Assert both complete (rc 0), and in push.log the
#      second tick's `push: start` line sits AFTER the first tick's `push:
#      end` line (a parallel port would log start, start, end, end).
#   2. PAGE at failure #1: the token file is renamed away -> the tick fails
#      (rc 1), the state is 1, push-tick.out carries `TICK FAIL` (#1) and a
#      PAGE line with the EXACT message shape (1 tick(s) in a row; see
#      <pushHome>/push.log), and pageCommand was run with that message
#      (the driver's stub appends its args to a file; the message is there).
#   3. no PAGE at #2 and #3: two more failing ticks -> state 3, and the
#      number of PAGE lines in push-tick.out is still 1.
#   4. PAGE at #6: failing to six -> state 6, total PAGE lines == 2.
#   5. RECOVERED: the token is restored, the tick succeeds (rc 0) -> a
#      `RECOVERED after 6 failed tick(s)` line and the state file back to 0.
#   6. rc pass-through: a failing tick exits 1, a passing one exits 0 (the
#      oneshot's contract: no Restart, the rc is the journal).
#   7. the state file's non-numeric reset, on the FAILURE path (M1150, W-1 of
#      the a4cb5d5a independent review): `garbage` in push-tick.state, a
#      FAILING tick -> rc 1, the state reads 1 and push-tick.out carries the
#      exact line `TICK FAIL rc=1 (#1)` (the reference's case-pattern reset:
#      garbage counts as 0, so the first failure records 1). The passing tick
#      does not test the reset: the success path always writes 0, so a misread
#      of the garbage is unobservable there -- the failure path reads the prior
#      state before incrementing, and is where the reset matters. The grep is
#      the ONE specific line `TICK FAIL rc=1 (#1)` (the tick prints
#      `TICK FAIL rc=<rc> (#<fails>) | <last log line>`): a misread state, say
#      7, would record `(#8)` and the specific grep stays red (the reviewer's
#      `return 7 + any` perturbation in tick_read_state fires exactly it). No
#      crash.
#   8. pageCommand ABSENT at a due page: a second config (same pushHome)
#      without the pageCommand key. Cell 8 resets the state to 0 first (its
#      own fixture) so its tick is failure #1 (a page IS due) -- the pin for
#      absent-pageCommand at a due page: the failing tick (rc 1) writes the
#      PAGE line (count +1) and runs nothing (the pageCommand stub's argument
#      file is unchanged, no error about it). M1150 W-1 moved cell 7 to the
#      failure path (it left the state at 1), which is why cell 8 must set
#      its own state. The nothing-runs half is shared with the
#      every-6th-not-due path, which cells 3-4 pin (state 2, 3: no page).
#   9. the build_exclusion capture leak (M1150), sanitizer-gated: the cell
#      runs ONLY when the harness binary carries the LeakSanitizer / __asan_
#      build markers (a plain build's "no report" would mean "nothing was
#      checked" -- the exact failure class the driver header names);
#      otherwise it t_skip_ones (an ok line with a # SKIP directive that
#      still counts toward the plan, so the denominator stays honest).
#      The host gate's own leakcheck (scripts/leakcheck.sh) runs this
#      driver under SAN, where the teeth are real. When it runs: the
#      token file is restored (cell 8 left it broken) and ONE passing
#      tick must exit 0 and carry no `ERROR: LeakSanitizer` on either
#      stream. On the unfixed
#      tree the tick leaks the per-tip cat-file -e capture that
#      build_exclusion's loop-end re-init abandoned on the failure path (a
#      tip not present in the mirror -- the expected case), 384 bytes in 3
#      objects, and the sanitizer's report flips the tick's exit code to 1,
#      so the leak is RED on the rc and on the quoted `Direct leak of
#      <N> byte(s) in <M> object(s)` line -- the H7 gate's RED.
#  10. a pageCommand that cannot be executed is reported on stderr (M1179):
#      the fork backend's child's exit 127 appears on stderr as
#      `pageCommand exited 127`, the PAGE line is still written, rc 1.
#  11. the push tick's push.git refresh lands (M1211, the note's b1-b4):
#      the standin has a master branch, the token is restored (cell 10 left
#      it renamed away), the state is reset to 0, a commit C2 is added to
#      the team's $PFX/main, the mirror's $PFX/main = C2, the standin M1.
#      After a PASSING tick: tracking $PFX/main == C2 == standin (b3);
#      local $PFX/main == C2 == mirror (b2); origin/master == standin master
#      (b1); push.log carries the exact b4 line with C2's short sha.
#  12. a non-FF (rewritten) mirror ref is NOT carried over (b2's NO `+`):
#      the MIRROR's $PFX/main is rewound to M1, the tick runs (team push
#      fails, mirror M1 -> standin C2 is non-FF), the tick is rc 1, state
#      1 -> 2, TICK FAIL (#2). Assert rc 1, push.log carries the b2 refusal
#      line ('the fetch from the drive's mirror failed'), and push.git's
#      local $PFX/main is STILL C2 (the rewound M1 was not carried over).
#  13. a failed remote fetch -> the tick fails, NOTHING pushed (b1's FATAL):
#      the standin is renamed away, the tick runs. Assert rc 1,
#      push.log carries 'the fetch of the remote's refs failed', and the
#      tick's push.log section has 'push: start' but NO timestamped
#      'push: end rc=' (b1's failure skips the pushes AND the end line).
#  14. a gate-style clone of push.git works (the consumer's shape): the
#      state is reset to 0, the mirror is re-synced to the standin, a
#      PASSING tick runs (empty range, C2->C2). Assert git clone + checkout
#      both rc 0, and the clone's HEAD == push.git's local $PFX/main ==
#      mirror's $PFX/main.
#
# THE FIXTURE. Under $SMOKE_TMP: an init-built rehearsal (src ->
# `init rehearsal`, the team/mirror/standin/recs layout the built M1055/
# M1053 code produces; mirror fetch populates the mirror) so the tick's
# team push (built, M1056) and records push have real local repos to run
# against -- no network, no podman. recs' origin is the rehearsal's
# standin.git (init's layout); recs' stored config copy
# (recs.git-config.harness-copy, B1.1) is written here as a byte copy of
# recs' live .git/config (init will write it itself once its extension
# lands; the URL guard compares bytes). A 9 MiB blob is committed on the
# records branch BEFORE any tick (cell 1's slow first push; the later
# ticks' scan range is empty, the remote already holds it).
# pushHome is $tmp/pushhome (created by the driver, the reference's
# setup-dir.sh shape); its push.log / push-tick.out / push-tick.state /
# push.lock all live there, per the config's host block.
# The config JSON is a minimal harness config (dir, modelKeyFile,
# gitTokenFile, drive.branchPrefix, the note's host block: pushHome, image,
# pageCommand). The red tree's loader parses point-wise and ignores the
# unknown host block, which is why the red lands on `unknown command
# 'push'`, not on a config refusal. modelKeyFile carries a random hex
# canary; gitTokenFile carries a DISTINCT fake 40-hex canary (never a real
# key -- a leaked canary can be attributed to this driver by its name).
# pageCommand is a driver-written sh stub that appends each of its args to
# $tmp/page-args (one per line), so the driver can assert the PAGE message
# reached it as an argument.
#
# Runs the tree's binary by path ($HBIN, the third_program idiom), never a
# PATH-resolved jichi-harness, and -- for smoke_lint check 5 -- also runs
# the tree's "$BIN" once. No timeout wrapper (local repos, bounded ticks);
# git children carry -c user.name/-c user.email (M2-neutral).
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"

t_plan 14
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
TOKEN_CANARY="a94f2c8e6d1b3f5a7c9e2d4b6f8a1c3e5d7b9a2f"
KEYCANARY=$(od -An -N20 -tx1 /dev/urandom | tr -d ' \n'); [ -n "$KEYCANARY" ] || KEYCANARY="canarykey$$_"

# A fixture failure emits one t_fail per cell so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt 13 ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

if [ ! -x "$HBIN" ]; then
    fixture_fail "the harness binary $HBIN is not built (run `make jichi-harness` first)"
fi

# --- the fixture: an init-built rehearsal, the tick's two pushes real ------
SRC="$tmp/src"
git init -q "$SRC" || fixture_fail "init src"
git -C "$SRC" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "source: plan base" || fixture_fail "src commit"
git -C "$SRC" branch "$PFX/plan" || fixture_fail "src plan branch"

# A FAKE pinned jichi (init's layout pins one; the tick never runs it).
printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' \
    > "$tmp/jichi" || fixture_fail "fake jichi"
chmod 755 "$tmp/jichi"

printf '%s\n' "$KEYCANARY" > "$tmp/kfile"; chmod 600 "$tmp/kfile"
printf '%s\n' "$TOKEN_CANARY" > "$tmp/tokfile"; chmod 600 "$tmp/tokfile"

PH="$tmp/pushhome"; mkdir -p "$PH" || fixture_fail "pushHome"

# The page stub: appends each argument, one per line, to $tmp/page-args.
printf '#!/bin/sh\nfor _a in "$@"; do printf %%s\\n "$_a" >> %s/page-args; done\nexit 0\n' \
    "$tmp" > "$tmp/page.sh" || fixture_fail "page stub"
chmod 755 "$tmp/page.sh"
: > "$tmp/page-args"

cat > "$tmp/cfg.json" <<EOF
{"dir":"$tmp/dir",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"drive":{"branchPrefix":"$PFX","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"host":{"pushHome":"$PH","image":"jichi-drive:test","pageCommand":"$tmp/page.sh"}}
EOF
# cfg8: the same config WITHOUT pageCommand (cell 8).
cat > "$tmp/cfg8.json" <<EOF
{"dir":"$tmp/dir",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"drive":{"branchPrefix":"$PFX","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"host":{"pushHome":"$PH","image":"jichi-drive:test"}}
EOF

"$HBIN" init --config "$tmp/cfg.json" rehearsal "$tmp/dir" \
    --from "$SRC" --jichi-bin "$tmp/jichi" \
    < /dev/null > "$tmp/i" 2> "$tmp/i.err" || \
    fixture_fail "init rehearsal failed: $(tr '\n' ' ' < "$tmp/i.err" | head_bytes 200)"
# A work branch on team (the mirror push's refspec carries $BR/main +
# $BR/work/*; a mirror without one would make the explicit refspec fail --
# the mirror_push driver creates these too).
git -C "$tmp/dir/team" branch -q "$PFX/work/a" "$PFX/main" \
    || fixture_fail "team work branch"
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/f" 2> "$tmp/f.err" || \
    fixture_fail "mirror fetch failed: $(tr '\n' ' ' < "$tmp/f.err" | head_bytes 200)"

# recs' stored config copy (B1.1), a byte copy of recs' live config (the URL
# guard compares bytes; init writes this once its extension lands).
cp "$tmp/dir/recs/.git/config" "$tmp/dir/recs.git-config.harness-copy" \
    || fixture_fail "recs stored copy"

# M1211: give the standin a master branch BEFORE the warm-up tick, because b1
# fetches refs/heads/master explicitly. The real remote has a master; the
# standin mirrors that shape.
git -C "$tmp/dir/standin.git" update-ref refs/heads/master \
    "$(git -C "$tmp/dir/standin.git" rev-parse refs/heads/$PFX/plan)" \
    || fixture_fail "standin master branch"

# One passing tick FIRST (before any big blobs), to make sure the fixture
# itself can run the tick's two pushes end-to-end. It also lands the initial
# small records state on recs' origin, so cell 1's second tick has an empty
# push range and only the lock is under test. On the red tree this warm-up
# exits 2 (`unknown command 'push'`) -- that is the red, NOT a fixture
# failure (cell 1 quotes it); the warm-up's rc is only a precondition for
# the fixture's own git seeding, which runs after it.
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t0" 2> "$tmp/t0.err"

# --- 1: two ticks 1 s apart are serialised by the lock -----------------------
# A 32 MiB urandom blob is committed on the records branch now, so the
# FIRST tick (background) holds push.lock for ~2 s (the key-scan cat-file
# loop over it + the push itself -- a local 32 MiB push measured ~1.6 s);
# the SECOND tick (foreground, started 1 s in) must WAIT for the lock, not
# fail and not run in parallel. Each tick logs, to push.log, one `push:
# start` line and TWO `push: end rc=` lines: the records push's own,
# untimestamped `push: end rc=N` (jc_hp_records.c rsay, written to the
# tick's stdout, which the tick redirects to push.log) and the tick's own,
# timestamped `<ts> push: end rc=N` (jc_hp_tick.c tick_append). The WARM-UP
# tick ran before cell 1's two, so the file's 2nd raw `push: end rc=` line
# is the warm-up's -- it always precedes the second tick's start, WITH OR
# WITHOUT the lock: comparing the last start against it is vacuous (the
# 2026-11-07 SV:2042 finding: a no-lock build passed the old assertion).
# The assertion therefore compares the last `push: start` line against the
# first tick's OWN end line -- the 2nd TIMESTAMPED `push: end rc=` (the
# warm-up's own end is the 1st, this tick's is the 2nd; the records lines
# carry no `<ts> ` prefix and are excluded). Serialised: start_a, end_a
# (own), start_b, end_b (own). If the lock were absent, the two pushes
# would overlap: start_a, start_b, end_*, end_* -- and the LAST `push:
# start` would sit BEFORE the first tick's own `push: end`. The assertion
# is exactly that: the last start line number > the 2nd timestamped end
# line number in push.log.
dd if=/dev/urandom of="$tmp/dir/recs/big32.bin" bs=1048576 count=32 2>/dev/null \
    || fixture_fail "c1 dd"
git -C "$tmp/dir/recs" add big32.bin
git -C "$tmp/dir/recs" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "records: a 32 MiB snapshot" || fixture_fail "c1 records commit"
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t1a" 2> "$tmp/t1a.err" &
_t1a_pid=$!
sleep 1
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t1b" 2> "$tmp/t1b.err"
rc1b=$?
wait "$_t1a_pid"
rc1a=$?
_laststart=$(grep -n "push: start" "$PH/push.log" 2>/dev/null | tail -1 | cut -d: -f1)
_ownend=$(grep -nE '^[0-9][0-9][0-9][0-9]-.*push: end rc=' "$PH/push.log" 2>/dev/null | sed -n '2p' | cut -d: -f1)
if [ "$rc1a" -ne 0 ] || [ "$rc1b" -ne 0 ]; then
    t_fail "1: the two ticks did not both complete (rc=$rc1a/$rc1b, want 0/0) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/t1a.err" | head_bytes 160) / $(tr '\n' ' ' < "$tmp/t1b.err" | head_bytes 160)"
elif [ -z "$_laststart" ] || [ -z "$_ownend" ]; then
    t_fail "1: push.log lacks the push: start / timestamped push: end lines to order (last-start='$_laststart' 1a-own-end='$_ownend') -- the tick's push log is absent: $(head_bytes 240 "$PH/push.log" 2>/dev/null)"
elif [ "$_laststart" -le "$_ownend" ]; then
    t_fail "1: the second tick's push: start (line $_laststart) is NOT after the first tick's own push: end (line $_ownend) -- the ticks overlapped: the lock did not serialise them: $(tr '\n' '|' < "$PH/push.log" | head_bytes 240)"
else
    t_ok "1: two ticks started 1 s apart are serialised by the lock -- both complete (rc 0/0), and in push.log the second tick's push: start (line $_laststart) is after the first tick's own push: end (line $_ownend): the second waited, it did not fail"
fi

# --- 2: PAGE at failure #1 (the token file renamed away) --------------------
mv "$tmp/tokfile" "$tmp/tokfile.broken" || fixture_fail "c2 break token"
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t2" 2> "$tmp/t2.err"
rc2=$?
_st2=$(cat "$PH/push-tick.state" 2>/dev/null)
_page_msg2="jichi drive: the host push failed (1 tick(s) in a row); see $PH/push.log"
if [ "$rc2" -ne 1 ]; then
    t_fail "2: the tick with a broken token file exited $rc2 (want 1) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/t2.err" | head_bytes 160)"
elif [ "$_st2" != "1" ]; then
    t_fail "2: the tick failed (rc 1) but push-tick.state is '$_st2' (want 1) -- the consecutive-failure count did not advance"
elif ! grep -qF "TICK FAIL" "$PH/push-tick.out" 2>/dev/null || ! grep -qF "(#1)" "$PH/push-tick.out" 2>/dev/null; then
    t_fail "2: push-tick.out has no \`TICK FAIL ... (#1)\` line for the first failure: $(head_bytes 240 "$PH/push-tick.out" 2>/dev/null)"
elif ! grep -qF "$_page_msg2" "$PH/push-tick.out" 2>/dev/null; then
    t_fail "2: push-tick.out has no PAGE line with the exact message \`$_page_msg2\`: $(tr '\n' '|' < "$PH/push-tick.out" | head_bytes 300)"
elif ! grep -qF "$_page_msg2" "$tmp/page-args"; then
    t_fail "2: the PAGE line was written but pageCommand was NOT run with the message (the stub's arg file does not carry \`$_page_msg2\`): $(tr '\n' '|' < "$tmp/page-args" | head_bytes 200)"
else
    t_ok "2: PAGE at failure #1 -- the broken token fails the tick (rc 1), the state is 1, push-tick.out carries TICK FAIL (#1) and the exact PAGE message, and pageCommand was run with that message"
fi

# --- 3: no PAGE at #2 and #3 -------------------------------------------------
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t3" 2> "$tmp/t3.err"
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t4" 2> "$tmp/t4.err"
rc34=$?
_st34=$(cat "$PH/push-tick.state" 2>/dev/null)
_np34=$(grep -c "PAGE" "$PH/push-tick.out" 2>/dev/null)
[ -n "$_np34" ] || _np34=0
if [ "$rc34" -ne 1 ]; then
    t_fail "3: the failing ticks #2/#3 did not exit 1 (rc=$rc34) -- the token is still broken: $(tr '\n' ' ' < "$tmp/t4.err" | head_bytes 160)"
elif [ "$_st34" != "3" ]; then
    t_fail "3: after failures #2 and #3 push-tick.state is '$_st34' (want 3) -- the count did not advance"
elif [ "$_np34" -ne 1 ]; then
    t_fail "3: after failure #3 push-tick.out has $_np34 PAGE line(s) (want 1) -- only the FIRST failure pages (and every 6th, cell 4): $(tr '\n' '|' < "$PH/push-tick.out" | head_bytes 240)"
else
    t_ok "3: no PAGE at failure #2 and #3 -- the state advances to 3 and the PAGE-line count stays 1 (the first failure paged, cells 2-3 do not)"
fi

# --- 4: PAGE at #6 (failing to six) ------------------------------------------
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t5" 2> "$tmp/t5.err"
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t6" 2> "$tmp/t6.err"
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t7" 2> "$tmp/t7.err"
rc7=$?
_st7=$(cat "$PH/push-tick.state" 2>/dev/null)
_np7=$(grep -c "PAGE" "$PH/push-tick.out" 2>/dev/null)
[ -n "$_np7" ] || _np7=0
if [ "$rc7" -ne 1 ]; then
    t_fail "4: the failing tick #6 did not exit 1 (rc=$rc7): $(tr '\n' ' ' < "$tmp/t7.err" | head_bytes 160)"
elif [ "$_st7" != "6" ]; then
    t_fail "4: after failure #6 push-tick.state is '$_st7' (want 6)"
elif [ "$_np7" -ne 2 ]; then
    t_fail "4: after failure #6 push-tick.out has $_np7 PAGE line(s) (want 2: #1 and #6) -- the every-6th re-page did not fire: $(tr '\n' '|' < "$PH/push-tick.out" | head_bytes 240)"
elif ! grep -qF "6 tick(s) in a row" "$PH/push-tick.out"; then
    t_fail "4: the second PAGE line does not name the count (want '6 tick(s) in a row'): $(grep -F "PAGE" "$PH/push-tick.out" | head_bytes 240)"
else
    t_ok "4: PAGE at #6 -- failing to six consecutive ticks pages again (total PAGE lines == 2), the message names 6 tick(s) in a row, the state is 6"
fi

# --- 5: RECOVERED (the token restored, the tick succeeds) --------------------
mv "$tmp/tokfile.broken" "$tmp/tokfile" || fixture_fail "c5 restore token"
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t8" 2> "$tmp/t8.err"
rc8=$?
_st8=$(cat "$PH/push-tick.state" 2>/dev/null)
if [ "$rc8" -ne 0 ]; then
    t_fail "5: the tick with the restored token exited $rc8 (want 0) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/t8.err" | head_bytes 160)"
elif ! grep -qF "RECOVERED after 6 failed tick(s)" "$PH/push-tick.out" 2>/dev/null; then
    t_fail "5: push-tick.out has no \`RECOVERED after 6 failed tick(s)\` line after the first success: $(tr '\n' '|' < "$PH/push-tick.out" | head_bytes 240)"
elif [ "$_st8" != "0" ]; then
    t_fail "5: after the recovering tick push-tick.state is '$_st8' (want 0)"
else
    t_ok "5: RECOVERED -- the restored token passes the tick (rc 0), push-tick.out carries \`RECOVERED after 6 failed tick(s)\` and the state file is back to 0"
fi

# --- 6: rc pass-through (a failing tick exits 1, a passing one exits 0) -----
mv "$tmp/tokfile" "$tmp/tokfile.broken" || fixture_fail "c6 break token"
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t9" 2> "$tmp/t9.err"
rc9=$?
mv "$tmp/tokfile.broken" "$tmp/tokfile" || fixture_fail "c6 restore token"
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t10" 2> "$tmp/t10.err"
rc10=$?
if [ "$rc9" -ne 1 ] || [ "$rc10" -ne 0 ]; then
    t_fail "6: rc pass-through failed (failing tick rc=$rc9 want 1, passing tick rc=$rc10 want 0): $(tr '\n' ' ' < "$tmp/t9.err" | head_bytes 160) / $(tr '\n' ' ' < "$tmp/t10.err" | head_bytes 160)"
else
    t_ok "6: rc pass-through -- a failing tick exits 1 and a passing one exits 0 (the oneshot's contract: the service passes the tick's rc through)"
fi

# --- 7: the state file's non-numeric reset, on the failure path (W-1) --------
# The reset is unobservable on a PASSING tick: the success path always writes
# 0, so a misread of `garbage` leaves no mark. The failure path reads the prior
# state before incrementing -- garbage must count as 0, so the first failure
# records 1 and the out line is exactly `TICK FAIL rc=1 (#1)` (the tick prints
# `TICK FAIL rc=<rc> (#<fails>) | <last log line>`). The grep is that ONE
# specific line, not `TICK FAIL` and `(#1)` separately: push-tick.out
# accumulates lines from cells 2-4, and a misread state, say 7, would record
# (#8) -- but the OPERATIVE tooth is the state assertion (state 1, want 1: a
# misread of garbage as 7 would record 8); the grep is belt-and-braces on the out line.
printf 'garbage\n' > "$PH/push-tick.state" || fixture_fail "c7 state write"
mv "$tmp/tokfile" "$tmp/tokfile.broken" || fixture_fail "c7 break token"
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t11" 2> "$tmp/t11.err"
rc11=$?
_st11=$(cat "$PH/push-tick.state" 2>/dev/null)
mv "$tmp/tokfile.broken" "$tmp/tokfile" || fixture_fail "c7 restore token"
if [ "$rc11" -ne 1 ]; then
    t_fail "7: a failing tick over a garbage state file exited $rc11 (want 1) -- the token is broken: $(tr '\n' ' ' < "$tmp/t11.err" | head_bytes 160)"
elif [ "$_st11" != "1" ]; then
    t_fail "7: after the failing tick push-tick.state is '$_st11' (want 1) -- \`garbage\` must be read as 0 (the reference's case pattern); a misparse of its digits would record 8 here"
elif ! grep -qF "TICK FAIL rc=1 (#1)" "$PH/push-tick.out" 2>/dev/null; then
    t_fail "7: push-tick.out lacks the specific \`TICK FAIL rc=1 (#1)\` line for the first failure over a garbage state -- a misread state would record (#8) instead: $(tr '\n' '|' < "$PH/push-tick.out" | head_bytes 240)"
else
    t_ok "7: the state file's non-numeric reset, on the failure path -- \`garbage\` in push-tick.state, a failing tick: rc 1, the state reads 1 (garbage counted as 0), push-tick.out carries the specific \`TICK FAIL rc=1 (#1)\` line, no crash (W-1: a passing tick would not test the reset -- the success path always writes 0)"
fi

# --- 8: pageCommand ABSENT at a due page (the tick is failure #1) ----------
# Cell 8 sets its OWN state to 0 before breaking the token, so its tick is
# failure #1 (a page IS due): M1150 W-1 moved cell 7 to the failure path (it
# left the state at 1), which is why cell 8 must reset it to run at #1 again.
# The pageCommand-ABSENT tick writes the PAGE line (count +1) and runs
# nothing: the page stub's arg file is unchanged, no error about the absent
# command -- the pin for absent-pageCommand at a due page. The nothing-runs
# half is shared with the every-6th-not-due path, which cells 3-4 pin (state
# 2, 3: no page).
printf '0\n' > "$PH/push-tick.state" || fixture_fail "c8 state write"
mv "$tmp/tokfile" "$tmp/tokfile.broken" || fixture_fail "c8 break token"
_np_before8=$(grep -c "PAGE" "$PH/push-tick.out" 2>/dev/null)
[ -n "$_np_before8" ] || _np_before8=0
_pa_before8=$(wc -c < "$tmp/page-args" 2>/dev/null); [ -n "$_pa_before8" ] || _pa_before8=0
"$HBIN" push tick --config "$tmp/cfg8.json" < /dev/null > "$tmp/t12" 2> "$tmp/t12.err"
rc12=$?
_np_after8=$(grep -c "PAGE" "$PH/push-tick.out" 2>/dev/null)
[ -n "$_np_after8" ] || _np_after8=0
_pa_after8=$(wc -c < "$tmp/page-args" 2>/dev/null); [ -n "$_pa_after8" ] || _pa_after8=0
if [ "$rc12" -ne 1 ]; then
    t_fail "8: the failing tick under the pageCommand-ABSENT config exited $rc12 (want 1) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/t12.err" | head_bytes 160)"
elif [ "$_np_after8" != "$((_np_before8 + 1))" ]; then
    t_fail "8: at failure #1 (a page IS due) the PAGE line was NOT written under the pageCommand-ABSENT config (count $_np_before8 -> $_np_after8, want +1) -- the line is the record, the command is the hook: $(tr '\n' '|' < "$PH/push-tick.out" | head_bytes 240)"
elif [ "$_pa_after8" != "$_pa_before8" ]; then
    t_fail "8: without pageCommand something STILL RAN (the stub's arg file grew $_pa_before8 -> $_pa_after8 bytes) -- absent pageCommand = line only, nothing runs"
else
    t_ok "8: pageCommand ABSENT at a due page -- the failing tick (rc 1) writes the PAGE line (count +1) and runs nothing: the page stub's argument file is unchanged, no error about the missing command"
fi

# --- 9: the build_exclusion capture leak (M1150), sanitizer-gated ---------
# The leak instrument is real only under a sanitizer build: a plain binary
# prints no LeakSanitizer report at all, so "no report" there would mean
# "nothing was checked" -- the exact failure class the driver header names.
# The host gate's own leakcheck (scripts/leakcheck.sh) runs this driver under
# SAN; on a non-sanitizer $HBIN the cell skips (but still counts to the plan).
if strings "$HBIN" 2>/dev/null | grep -qE 'LeakSanitizer|__asan_'; then
    # One PASSING tick. The token file must be present again: cell 8 renamed
    # it away (and never restored it), so put it back first -- a passing tick
    # needs the token to be loadable. On the unfixed tree this tick's
    # records-push build_exclusion leaks the per-tip cat-file -e capture the
    # loop-end re-init abandoned, and the sanitizer's report flips the tick's
    # exit code from 0 to 1.
    mv "$tmp/tokfile.broken" "$tmp/tokfile" || fixture_fail "c9 restore token"
    "$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t13" 2> "$tmp/t13.err"
    _rc13=$?
    if [ "$_rc13" -ne 0 ] || grep -q 'ERROR: LeakSanitizer' "$tmp/t13" "$tmp/t13.err" 2>/dev/null; then
        _leak=$(grep -m1 'Direct leak' "$tmp/t13.err" 2>/dev/null)
        [ -n "$_leak" ] || _leak=$(head_bytes 160 "$tmp/t13.err")
        t_fail "9: the passing tick is not leak-clean (rc=$_rc13): $_leak -- build_exclusion's per-tip cat-file -e capture the loop-end re-init abandoned (M1150)"
    else
        # The ok line must NOT name the sanitizer's report marker verbatim: the
        # host gate's own leakcheck greps a driver's stdout for exactly that
        # string, so an ok that prints it would red a clean driver (measured).
        t_ok "9: a passing tick is leak-clean -- no sanitizer leak report on either stream (build_exclusion's per-tip cat-file -e capture freed on every path, M1150)"
    fi
else
    t_skip_one "9: a passing tick is leak-clean (the build_exclusion capture leak, M1150) -- $HBIN is not a sanitizer build (the leak instrument is only real under ASan; the host gate's leakcheck runs it)"
fi

# --- 10 (M1179): a pageCommand that cannot be executed is reported ---------
# On the default fork backend a command that cannot be executed is the child's
# exit 127 (jc_spawn's _exit(127) after the failed exec), and tick_run_page
# reported only a spawn failure (rc < 0): a misspelt pageCommand paged nobody
# and said nothing. Now any non-zero exit is reported on stderr, 127 named.
# The backend is pinned to fork (any value but spawn/posix selects it), the
# one where the silence was. The PAGE line is still written: the line is the
# record, the command is the hook. Red on the pre-M1179 binary: stderr has no
# pageCommand line.
cat > "$tmp/cfg10.json" <<EOF
{"dir":"$tmp/dir",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"drive":{"branchPrefix":"$PFX","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"host":{"pushHome":"$PH","image":"jichi-drive:test","pageCommand":"$tmp/no-such-page.sh"}}
EOF
if [ -f "$tmp/tokfile" ]; then
    mv "$tmp/tokfile" "$tmp/tokfile.broken" || fixture_fail "c10 break token"
fi
printf '0\n' > "$PH/push-tick.state" || fixture_fail "c10 state write"
_np_before10=$(grep -c "PAGE" "$PH/push-tick.out" 2>/dev/null)
[ -n "$_np_before10" ] || _np_before10=0
JICHI_SPAWN_BACKEND=fork "$HBIN" push tick --config "$tmp/cfg10.json" \
    < /dev/null > "$tmp/t10" 2> "$tmp/t10.err"
rc10=$?
_np_after10=$(grep -c "PAGE" "$PH/push-tick.out" 2>/dev/null)
[ -n "$_np_after10" ] || _np_after10=0
if [ "$rc10" -ne 1 ]; then
    t_fail "10: the failing tick with an unexecutable pageCommand exited $rc10 (want 1): $(tr '\n' ' ' < "$tmp/t10.err" | head_bytes 160)"
elif [ "$_np_after10" != "$((_np_before10 + 1))" ]; then
    t_fail "10: the PAGE line was NOT written (count $_np_before10 -> $_np_after10, want +1)"
elif ! grep -qF "pageCommand exited 127" "$tmp/t10.err"; then
    t_fail "10: an unexecutable pageCommand was SILENT on the fork backend (no 'pageCommand exited 127' on stderr): $(tr '\n' '|' < "$tmp/t10.err" | head_bytes 200)"
else
    t_ok "10: a pageCommand that cannot be executed (fork backend, the child's exit 127) is reported on stderr as 'pageCommand exited 127', the PAGE line is still written, rc 1 (M1179)"
fi

# --- 11: the refresh lands (M1211, b1-b4) ----------------------------------
# Standing state entering cell 11 (left by cell 10): the token file is
# RENAMED AWAY, the state file reads 1. The mirror and the standin are in
# sync at M1 (the tip the warm-up and cells 5-6 pushed); the standin's master
# branch points at the plan tip (the fixture's new setup, above).
# Setup: restore the token; reset the state to 0; record M1 = the standin's
# $PFX/main; add a commit C2 to the team's $PFX/main; mirror fetch; run a
# PASSING tick. Assert the four items (b1-b4).
mv "$tmp/tokfile.broken" "$tmp/tokfile" 2>/dev/null || fixture_fail "c11 restore token"
printf '0\n' > "$PH/push-tick.state" || fixture_fail "c11 state write"
M1=$(git -C "$tmp/dir/standin.git" rev-parse "$PFX/main") || fixture_fail "c11 get M1"
# Add a commit C2 to the team's $PFX/main
printf 'c2 content\n' >> "$tmp/dir/team/c2.txt"
git -C "$tmp/dir/team" add c2.txt
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team: commit C2" || fixture_fail "c11 commit C2"
# Mirror fetch: the mirror's $PFX/main = C2, the standin still M1
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/f11" 2> "$tmp/f11.err" || fixture_fail "c11 mirror fetch"
# Run the PASSING tick
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t11" 2> "$tmp/t11.err"
rc11=$?
# Assert: tracking $PFX/main == C2 == standin (b3)
_tracking=$(git -C "$PH/push.git" rev-parse -q --verify "refs/remotes/origin/$PFX/main" 2>/dev/null)
_stand_main=$(git -C "$tmp/dir/standin.git" rev-parse -q --verify "$PFX/main" 2>/dev/null)
_c2=$(git -C "$tmp/dir/team" rev-parse -q --verify "$PFX/main" 2>/dev/null)
if [ -z "$_tracking" ]; then
    t_fail "11: push.git has no refs/remotes/origin/$PFX/main (the tick's refresh is absent)"
elif [ "$_tracking" != "$_c2" ] || [ "$_tracking" != "$_stand_main" ]; then
    t_fail "11: push.git has no refs/remotes/origin/$PFX/main (the tick's refresh is absent) -- tracking='$_tracking' want '$_c2'='$_stand_main'"
else
    t_ok "11: the refresh lands -- tracking refs/remotes/origin/$PFX/main == C2 == standin (b3)"
fi

# --- 12: a non-FF (rewritten) mirror ref is NOT carried over (b2) -----------
# Setup: rewind the MIRROR's $PFX/main to M1, run the tick. Assert rc 1,
# push.log carries the b2 refusal line, and push.git's local $PFX/main is STILL C2.
git -C "$tmp/dir/mirror.git" update-ref "refs/heads/$PFX/main" "$M1" \
    || fixture_fail "c12 rewind mirror"
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t12" 2> "$tmp/t12.err"
rc12=$?
_mirror_local=$(git -C "$PH/push.git" rev-parse "$PFX/main" 2>/dev/null)
if [ "$rc12" -ne 1 ]; then
    t_fail "12: the tick with a rewound mirror exited $rc12 (want 1)"
elif ! grep -qF "the fetch from the drive's mirror failed" "$PH/push.log" 2>/dev/null; then
    t_fail "12: push.log has no 'the fetch from the drive's mirror failed' line (the mirror fetch is absent)"
elif [ "$_mirror_local" != "$_c2" ]; then
    t_fail "12: push.git refs/heads/$PFX/main is '$_mirror_local' (want '$_c2' -- the rewound M1 was not carried over)"
else
    t_ok "12: a non-FF mirror ref is NOT carried over (b2) -- the tick fails (rc 1), push.log carries the b2 refusal line, push.git's local $PFX/main is STILL C2"
fi

# --- 13: a failed remote fetch -> the tick fails, NOTHING pushed (b1) -------
# Setup: rename the standin away, run the tick. Assert rc 1,
# push.log carries 'the fetch of the remote's refs failed', and the tick's
# push.log section has 'push: start' but NO timestamped 'push: end rc='.
mv "$tmp/dir/standin.git" "$tmp/dir/standin.git.broken" || fixture_fail "c13 rename standin"
# Count timestamped 'push: end rc=' lines before the tick
_end_before=$(grep -cE '^[0-9][0-9][0-9][0-9]-.*push: end rc=' "$PH/push.log" 2>/dev/null)
[ -n "$_end_before" ] || _end_before=0
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t13" 2> "$tmp/t13.err"
rc13=$?
_end_after=$(grep -cE '^[0-9][0-9][0-9][0-9]-.*push: end rc=' "$PH/push.log" 2>/dev/null)
[ -n "$_end_after" ] || _end_after=0
if [ "$rc13" -ne 1 ]; then
    t_fail "13: the tick exited $rc13 (want 1) with the remote renamed away (the remote fetch is absent, so the tick did not fail)"
elif ! grep -qF "the fetch of the remote's refs failed" "$PH/push.log" 2>/dev/null; then
    t_fail "13: push.log has no 'the fetch of the remote's refs failed' line (the remote fetch is absent)"
elif [ "$_end_after" -ne "$_end_before" ]; then
    t_fail "13: the tick wrote 'push: end rc=' lines (count $_end_before -> $_end_after), but b1's failure skips the end line"
else
    t_ok "13: a failed remote fetch -> the tick fails, NOTHING pushed (b1) -- rc 1, push.log carries 'the fetch of the remote's refs failed', no timestamped 'push: end rc='"
fi
# Restore the standin
mv "$tmp/dir/standin.git.broken" "$tmp/dir/standin.git" || fixture_fail "c13 restore standin"

# --- 14: a gate-style clone of push.git works (the consumer's shape) --------
# Setup: reset the state to 0; re-sync the mirror to the standin; run a
# PASSING tick (empty range). Assert git clone + checkout both rc 0.
printf '0\n' > "$PH/push-tick.state" || fixture_fail "c14 state write"
git -C "$tmp/dir/mirror.git" fetch -q origin "+refs/heads/$PFX/main:refs/heads/$PFX/main" \
    || fixture_fail "c14 re-sync mirror"
"$HBIN" push tick --config "$tmp/cfg.json" < /dev/null > "$tmp/t14" 2> "$tmp/t14.err"
rc14=$?
git clone -q "$PH/push.git" "$tmp/cl14" || fixture_fail "c14 clone"
git -C "$tmp/cl14" checkout -q "$PFX/main" 2>/dev/null
clone_head=$(git -C "$tmp/cl14" rev-parse HEAD 2>/dev/null)
push_local=$(git -C "$PH/push.git" rev-parse "$PFX/main" 2>/dev/null)
mirror_local=$(git -C "$tmp/dir/mirror.git" rev-parse "$PFX/main" 2>/dev/null)
if [ "$rc14" -ne 0 ]; then
    t_fail "14: the PASSING tick exited $rc14 (want 0)"
elif ! git -C "$tmp/cl14" rev-parse --verify "$PFX/main" >/dev/null 2>&1; then
    t_fail "14: the clone of push.git has no $PFX/main branch to checkout (the tick's mirror fetch is absent)"
elif [ "$clone_head" != "$push_local" ] || [ "$clone_head" != "$mirror_local" ]; then
    t_fail "14: the clone's HEAD '$_clone_head' != push.git's local '$_push_local' != mirror's '$_mirror_local'"
else
    t_ok "14: a gate-style clone of push.git works -- clone + checkout rc 0, HEAD == push.git's local == mirror's"
fi

t_done
