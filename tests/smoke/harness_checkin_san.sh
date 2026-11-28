#!/bin/sh
# smoke: `jichi-harness checkin` under the project's own SAN policy
# (M1228, batch H13 follow-up; the leak's red-first tooth; the 586th driver)
#
# WHY (the M1228 leak). The checkin verb's C15 PAGE block (push-tick.out:
# :706-750), C20b PAGE block (page-watch.out: :936-984), and C7 harness.log
# tail (:500-543) keep every line in memory but only print a tail; they free
# only the printed tail and leak the kept-but-unprinted rest. The host's
# leakcheck caught it as a 22-byte Direct leak from the C15 PAGE block's first
# of the driver's three PAGE lines. The main driver (harness_checkin.sh) runs
# the NON-SAN binary so it cannot see the leak; this driver is the in-tree
# tooth the gate's leakcheck stage also runs.
#
# THE MEASURED RECIPE (run 0129, b14be77d8, warm: the SAN build is ONE worktree
# build, MEASURED 5 s on this host but 2-3 min cold). This driver's wall is ONE
# worktree build + one checkin run under ASan; the worktree build dominates, so
# it sits in run.sh's 600 s list, BEFORE JC_SMOKE_TIMEOUT_MULT. No timeout
# wrapper around the checkin itself: the run must be capped by nothing, and a
# SAN abort must surface as the driver's own `not ok`, never as a deadline
# artifact.
#
# THE SKIP SPLIT (M1203-i, i-3, kept honest). A committed regression cell that
# never runs is VACUOUS: the only t_skip in this driver is the toolchain one --
# a toolchain whose compiler/linker does not speak ASan (an unrecognized-option
# or an ASan-link failure, ONE grep on the captured make log) cannot build the
# thing under test, so skipping is the true verdict there. Any OTHER make
# failure is a t_fail (the make output IS the evidence), and a make that exits
# 0 but built nothing is a t_fail on the next check (the strings probe).
# `command -v` on git and make are whole-driver verbs BEFORE t_plan: no git, no
# worktree; no make, no build -- both are "cannot run at all", not "cannot run
# this way".
#
# WHAT IS CHECKED (t_plan 3, one t_ok/t_fail each):
#   1. the SAN build is REAL: `strings <wt>/jichi-harness` holds an __asan_
#      / LeakSanitizer symbol. If this fails, the "sanitized" binary is not
#      -- every later verdict is void (the run would pass on the unpoisoned
#      bytes that are intact without instrumentation).
#   2. the checkin COMPLETES rc 0 under ASan (ASAN_OPTIONS=exitcode=0 env-prefix,
#      the stranded_call_uaf.sh form): exitcode=0 (NOT abort_on_error=0, which
#      does not force the exit code) because a LEAK does not abort the process
#      -- LSan reports at exit and returns a non-zero code by default, so
#      exitcode=0 makes check 2 "completes without aborting" the real guard
#      (a fix that turned the leak into a crash would red here) and leaves the
#      leak detection to check 3.
#   3. NEITHER capture holds a leak report (`Direct leak`,
#      `ERROR: LeakSanitizer`, or `SUMMARY: AddressSanitizer`).
#
# FIXTURE (one checkin run trips all three sites at once). A drive dir D with
# harness.log of EIGHT lines (so the C7 tail-6 frees nothing of the first two),
# runs/0121 with a meta.txt + telemetry.jsonl (one model_call line) + stream.jsonl
# (one text line), inbox/; a host dir H with push-tick.out holding one ordinary
# line + THREE PAGE lines (first line exactly `PAGE: alpha line one` -- the
# 22-byte leak), page-watch.out holding THREE PAGE lines, NO supervision-log.md
# on either side (both-missing arm: nothing printed, no interference), NO
# follow command in the config. The config: dir=$D, modelKeyFile a canary file
# (printf a 40-hex canary into $D/key, chmod 600 -- the redactor will show it
# as <r>, never a real key), host{dir=$H, pushHome=$H}, drive{loopUnit=jhp-san-loop,
# onceUnit=jhp-san-loop-once, containerName=jhp-san}. NO host.checkinFollow, NO
# host.pushRepo/drive.branchPrefix (the C17 section is omitted), NO host.logPath
# (C18 omitted).
#
# STATE: the checkin verb never rms, so nothing is cleaned between steps;
# there is one checkin run. The worktree is removed at the end explicitly
# AND by smoke_tmp's own cleanup (the worktree lives under the smoke_tmp dir,
# so the registry's rm -rf covers it; the explicit `git worktree remove --force`
# + `prune` is belt and braces for a run.sh kill mid-build).
#
# smoke_lint check 5: a non-lint driver must run the tree's "$BIN" --
# satisfied at SOURCE level (the lint greps for the quoted "$BIN"). This
# driver's SUBJECT is the tree's jichi-harness built in its OWN worktree
# (the leak lives in the harness, not in jichi); the tree's "$BIN" is
# exercised as the source-level no-op, as the sibling
# harness_push_argv_boundary.sh does.
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
# Ambient harness loader settings (env wins over the config) and the
# operator's ambient git config for the fixture repos.
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0
export GIT_CLONE_PROTECTION_ACTIVE=0

# A fixture failure emits one t_fail per check so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt 3 ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

# --- the WORKTREE (the M1203-i shape: the tree's own head, self-contained) ----
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

# --- the SAN build (the measured recipe; the driver's wall, ~2-3 min) --------
# One grep keeps the skip discrimination from going vacuous: the build MUST
# have run (a make that exits 0 but built nothing is a t_fail on check 1).
_mk=$(smoke_make)
(
    cd "$WT" || exit 3
    exec "$_mk" -j8 SAN=1 WERROR=1 jichi-harness
) > "$tmp/san.build.log" 2>&1
SBRC=$?
SANBIN="$WT/jichi-harness"

if [ "$SBRC" -ne 0 ] || [ ! -x "$SANBIN" ]; then
    # TOOLCHAIN-SILENT = the toolchain does not speak ASan (the compiler
    # rejects the -fsanitize option -- clang's `unknown argument`, gcc's
    # `unrecognized command-line option` -- or the linker cannot find the
    # ASan runtime: `cannot find -lasan`, the missing `libclang_rt.asan*`).
    # The driver cannot build its subject there, so a skip is the true
    # verdict; the ONE grep on the captured log keeps it from going vacuous
    # (it matches ONLY the error lines: make's ECHOED compile line carries
    # -fsanitize too, so the pattern must not name it). ANY other failure
    # is a t_fail -- a committed cell that fails to build is evidence, not
    # an absence (M1203-i's in-stage rule). A make that exits 0 but built
    # nothing falls to the same t_fail ([ ! -x "$SANBIN" ]); check 1
    # re-verifies the symbols in the ordinary case.
    TOOLCHAIN_SILENT=0
    if "$G" -qiE 'unknown argument|unknown option|unrecogni[sz]ed (command-line )?option|cannot find -lasan|libclang_rt\.asan' "$tmp/san.build.log"; then
        TOOLCHAIN_SILENT=1
    fi
    if [ "$TOOLCHAIN_SILENT" -eq 1 ]; then
        # Mid-driver, so t_skip_one (the per-check verb that keeps the
        # denominator honest) rather than the whole-driver t_skip (a
        # second `1..0` plan line after `1..3` is the M672 shape
        # smoke_lint check 20 refuses).
        t_skip_one "1: the SAN build is real -- SKIP: the toolchain cannot build the SAN subject (unrecognized -fsanitize option or ASan link failure; the make log is $tmp/san.build.log)"
        t_skip_one "2: the checkin completes rc 0 under ASan -- SKIP: the toolchain cannot build the SAN subject"
        t_skip_one "3: NEITHER capture holds a leak report -- SKIP: the toolchain cannot build the SAN subject"
        t_done
    fi
    t_fail "1: the SAN build failed (rc=$SBRC; binary present: $([ -x "$SANBIN" ] && echo yes || echo no)) -- a committed regression cell that cannot build is evidence, not an absence; the make log's tail: $(tail -5 "$tmp/san.build.log" | tr '\n' '|' | head_bytes 300)"
    t_fail "2: the checkin completes rc 0 under ASan (NOT RUN -- the build failed)"
    t_fail "3: NEITHER capture holds a leak report (NOT RUN -- the build failed)"
    t_done
fi

# --- the fixture -------------------------------------------------------------
D="$tmp/drive"
H="$tmp/pushhome"
SHIM="$tmp/shim"
CFG="$D/conf/harness.json"
KEY="$D/key"

# Create drive dir and host dir
mkdir -p "$D/runs/0121" "$D/runs/0121/notes" "$D/inbox" "$D/conf" "$H" || fixture_fail "mkdir failed"

# KEY: the model key file (a 40-hex canary; redactor shows <r>)
printf 'checkinCANARY0123456789abcdef01234567\n' > "$KEY"
chmod 600 "$KEY"

# unitState.sh: returns state from flag file, exit 0
mkdir -p "$SHIM" || fixture_fail "mkdir -p SHIM failed"
printf '#!/bin/sh\ncat "%s" 2>/dev/null; exit 0\n' "$tmp/ustate" > "$SHIM/unitState.sh"

# push-tick.out fixture: one ordinary line + 3 PAGE lines (tail -2 keeps 2, first PAGE is the 22-byte leak)
cat > "$tmp/push-tick.out" << 'EOM'
2026-11-26 07:00:00 +0000 ordinary tick
PAGE: alpha line one
PAGE: beta line two
PAGE: gamma line three
EOM

# page-watch.out fixture: 3 PAGE lines
cat > "$tmp/page-watch.out" << 'EOM'
2026-11-26 07:00:00 +0000 PAGE: page line one
2026-11-26 07:00:00 +0000 PAGE: page line two
2026-11-26 07:00:00 +0000 PAGE: page line three
EOM

# harness.log fixture: 8 lines (tail -6 frees nothing of first two = 44 bytes leaked by C7)
cat > "$D/harness.log" << 'EOM'
2026-11-26 07:00:00 +0000 line 1 of harness.log
2026-11-26 07:00:00 +0000 line 2 kept
2026-11-26 07:00:00 +0000 line 3 kept
2026-11-26 07:00:00 +0000 line 4 kept
2026-11-26 07:00:00 +0000 line 5 kept
2026-11-26 07:00:00 +0000 line 6 kept
2026-11-26 07:00:00 +0000 line 7 kept
2026-11-26 07:00:00 +0000 line 8 kept
EOM

# runs/0121 fixtures
printf 'meta entry 1\n' > "$D/runs/0121/meta.txt"
cat > "$D/runs/0121/telemetry.jsonl" << 'EOM'
{"event":"model_call","ts":1000.0,"latency_ms":0,"ok":true,"status":200,"depth":0}
EOM
cat > "$D/runs/0121/stream.jsonl" << 'EOM'
{"event":"text","ts":1000.0,"text":"some text output"}
EOM

# inbox fixture
printf 'inbox entry 1\n' > "$D/inbox/entry1.md"

# --- config builder ------------------------------------------------------------
cfg_write() {
    _cfg_loop_unit="$1"
    _cfg_once_unit="$2"
    _cfg_container="$3"
    _cfg_base='{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","pushHome":"'"$tmp"'"},"drive":{"loopUnit":"'"$_cfg_loop_unit"'","onceUnit":"'"$_cfg_once_unit"'","containerName":"'"$_cfg_container"'"}}'
    echo "$_cfg_base" > "$CFG"
}

# Write config
cfg_write "jhp-san-loop" "jhp-san-loop-once" "jhp-san"

# --- 1: the SAN build is REAL (strings: __asan_ / LeakSanitizer) --------------
if strings "$SANBIN" 2>/dev/null | "$G" -qE '__asan_|LeakSanitizer'; then
    t_ok "1: the SAN build is real -- $SANBIN holds an __asan_/LeakSanitizer symbol (every later verdict is measured against a sanitized binary)"
else
    t_fail "1: the SAN build is NOT sanitized -- $SANBIN holds no __asan_/LeakSanitizer symbol (a make that exits 0 and built nothing, or a toolchain that accepted -fsanitize and silently ignored it); every later verdict is void, so the checks below fail"
fi

# --- 2: the checkin completes rc 0 under ASan ---------------------------------
# The env-prefix form (stranded_call_uaf.sh: `env NAME=value cmd`, NOT an
# assignment prefix -- FreeBSD's /bin/sh would not export it). No timeout
# wrapper: the run is capped by nothing, and the leak must surface as this
# driver's own verdict.
# exitcode=0: the leak being tested (C7 tail-6 freeing nothing) is inherent
# to the checkin verb; we want the process to complete rc 0 and print the
# leak report to stderr, not abort with a non-zero exit code.
env ASAN_OPTIONS=exitcode=0 "$SANBIN" --config "$CFG" checkin < /dev/null > "$tmp/out" 2> "$tmp/err"
RC=$?

# --- 3: NEITHER capture holds a leak report -----------------------------------
# The leak signatures (the note's union):
#   Direct leak
#   ERROR: LeakSanitizer
#   SUMMARY: AddressSanitizer
LEAK_IN_OUT=0
if "$G" -qE 'Direct leak|ERROR: LeakSanitizer|SUMMARY: AddressSanitizer' "$tmp/out"; then
    LEAK_IN_OUT=1
fi
LEAK_IN_ERR=0
if "$G" -qE 'Direct leak|ERROR: LeakSanitizer|SUMMARY: AddressSanitizer' "$tmp/err"; then
    LEAK_IN_ERR=1
fi

if [ "$RC" -eq 0 ]; then
    t_ok "2: the checkin completes rc 0 under ASan (ASAN_OPTIONS=exitcode=0, no timeout wrapper)"
else
    t_fail "2: the checkin under ASan expected rc 0 (got rc=$RC)"
fi

if [ "$LEAK_IN_OUT" -eq 0 ] && [ "$LEAK_IN_ERR" -eq 0 ]; then
    t_ok "3: NEITHER capture holds a leak report (the three signatures are absent: the Direct-leak line, the LeakSanitizer error header, the AddressSanitizer summary line)"
else
    t_fail "3: NEITHER capture should hold a leak report (out=$LEAK_IN_OUT err=$LEAK_IN_ERR; out head: $(tr '\n' '|' < "$tmp/out" | head_bytes 300); err head: $(tr '\n' '|' < "$tmp/err" | head_bytes 300))"
fi

t_done
