#!/bin/sh
# smoke: `jichi-harness repin <SHA8>` under the project's OWN SAN policy
# (M1203-i, i-3, the SAN leg) -- the 576th driver.
#
# WHAT IT GUARDS. i-3: in the repin port, the live-doctor child's env slot
# (`char ke[14 + RR_CAP_KEY + 1]` + its pointer `kvp`, jc_harness_repin.c's
# rr_run_cmd) was declared INSIDE the key-present block, whose scope ends
# BEFORE the jc_proc_capture fork. ASan poisons `ke` at block exit; the child
# inherits the poison and ABORTS in child_apply_env when it string-scans the
# entry (`ERROR: AddressSanitizer: stack-use-after-scope`, the run 0086
# reproduction). Uninstrumented the bytes are still intact, so the normal
# build passes and the value is delivered correctly -- latent, formally UB,
# aborting under the project's own SAN policy (make ci: -fno-sanitize-recover=
# all). The main driver (harness_repin.sh) cannot see it: it runs the tree's
# NON-SAN binary, and no make-ci stage ever enters the harness subcommand
# dispatch under a sanitizer (the unit suite stops before it; leakcheck's
# fixed subcommand list has no repin). This driver is the gate's tooth:
# `make ci` reaches it through its own smoke stage.
#
# WHY A WORKTREE, NOT A MAKEFILE STAGE (the note's "Why no Makefile stage").
# The first plan was a def-350-shaped `smoke-repin-san` ci stage with the
# driver's own `make clean` + SAN build + `make clean`. Two properties of
# THIS driver killed it: smoke_lint's orphan check (every tests/smoke/*.sh
# must be named in run.sh) forces tier registration, and a tier driver that
# runs `make clean` in the tier's tree deletes the tier's binaries mid-run
# (the keep-going incident's class). The worktree shape makes both go away:
# the driver is tier-registered (the 576th) and self-contained -- its OWN
# git worktree of the tree, its OWN objects, its OWN build. The worktree's
# objects stay inside the worktree, so a driver deleting the tier's binaries
# is impossible by construction.
#
# THE MEASURED RECIPE (run 0086, worktree /tmp/m1203i/sanw at b26fc811:
# `make SAN=1 jichi-harness` rc 0, ~2-3 min for the harness link; the jichi
# binary is NOT needed -- the harness link is self-contained). This driver's
# wall is ONE worktree build + one repin run; the worktree build dominates,
# so it sits in run.sh's 120 s list, BEFORE JC_SMOKE_TIMEOUT_MULT. No
# timeout wrapper around the repin itself: the run must be capped by
# nothing (the stranded_call_uaf.sh form), and a SAN abort must surface as
# the driver's own `not ok`, never as a deadline artifact.
#
# THE SKIP SPLIT (def-350's in-stage rule, kept honest). A committed
# regression cell that never runs is VACUOUS: the only t_skip in this
# driver is the toolchain one -- a toolchain whose compiler/linker does not
# speak ASan (an unrecognized-option or an ASan-link failure, ONE grep on
# the captured make log) cannot build the thing under test, so skipping is
# the true verdict there. Any OTHER make failure is a t_fail (the make
# output IS the evidence), and a make that exits 0 but built nothing is a
# t_fail on the next check (the strings probe). `command -v` on git and
# make are whole-driver verbs BEFORE t_plan: no git, no worktree; no make,
# no build -- both are "cannot run at all", not "cannot run this way".
#
# WHAT IS CHECKED (t_plan 3, one t_ok/t_fail each):
#   1. the SAN build is REAL: `strings <wt>/jichi-harness` holds an __asan_
#      / LeakSanitizer symbol. If this fails, the "sanitized" binary is
#      not -- every later verdict is void (the run would pass on the
#      unpoisoned bytes that are intact without instrumentation).
#   2. the repin COMPLETES rc 0 under ASan (ASAN_OPTIONS=abort_on_error=0
#      env-prefix, the stranded_call_uaf.sh form): NEITHER the captured
#      stdout NOR the stderr may hold `ERROR: AddressSanitizer` or
#      `stack-use-after-scope` -- i-3's child abort lands in the captured
#      live-doctor output (the verb's captured child, rc 1 -> doctors NOT
#      clean -> the run's rc 1), so both captures are scanned.
#   3. the key delivery WORKED: the live-doctor stand-in's argv dump holds
#      NO canary substring (the key never on argv, b20's teeth, the main
#      driver's cell-17 form) and the captured output holds the
#      `JICHI_API_KEY length=<n>` env line -- i-3's abort must not be
#      MASKED as a missing env: under the defect the child dies in
#      child_apply_env before it prints its env line, so check 3 fails
#      there too (two independent reds for one root cause, as the note
#      wants).
#
# FIXTURE (real git, the main driver's form, TWO commits only): seed with
# C1 (the old pin) + C2 (the target S8, a src/harness file changed) at
# fixed dates + -c user.name/-c user.email -> bare -> push.git clone + an
# explicit `git fetch origin` (b3's convention: origin/master exists only
# after a fetch; the bare's master is set to C2, so S8 IS master). A drive
# dir D with pins/<old8>/ (plain files), bin/ (copies), pins/current ->
# <old8>, conf/ (the harness.json + the doctor's jichi-config.json), a key
# file holding a CANARY (NEVER a real key; its own string, a distinct
# 12-char prefix from the main driver's "repinCANARY0": "sanrepinCANARY"),
# and a ci log dir with the fabricated `make ci rc=0` meta for S8. The
# shimmable commands are stand-ins (the container cannot run the host's
# real make / podman forms, CHANGED's transport-not-the-check): build-ok
# writes the two fixture binaries (both --version END on their build line,
# the port's measured shape; i-1's any-line neighbour is the main driver's
# cell 20, not this one's), hdoctor-ok prints the clean summary, live-argv
# dumps its argv to a file, prints the JICHI_API_KEY LENGTH from the env,
# then the clean summary, exit 0 (the main driver's live-argv.sh form).
# The .gitignore covers jichi + jichi-harness (the build stand-in writes
# them into the clone; b9's porcelain refusal would otherwise fire).
#
# STATE: the repin verb never rms (b16 binds the VERB, not the fixture), so
# nothing is cleaned between steps; there is one repin run. The worktree is
# removed at the end explicitly AND by smoke_tmp's own cleanup (the
# worktree lives under the smoke_tmp dir, so the registry's rm -rf covers
# it; the explicit `git worktree remove --force` + `prune` is belt and
# braces for a run.sh kill mid-build, which leaves the METADATA in the
# MAIN repo's .git/worktrees/ that only git can remove).
#
# smoke_lint check 5: a non-lint driver must run the tree's "$BIN" --
# satisfied at SOURCE level (the lint greps for the quoted "$BIN"). This
# driver's SUBJECT is the tree's jichi-harness built in its OWN worktree
# (the i-3 defect lives in the harness, not in jichi); the tree's "$BIN"
# is exercised as the source-level no-op, as the sibling
# harness_push_argv_boundary.sh does (its line is a no-op inside the stage,
# where only jichi-harness is built; in ordinary smoke it runs).
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"
command -v make >/dev/null 2>&1 || t_skip "make not available"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# smoke_lint check 5's source-level "$BIN" (see the header). In the 120-s
# tier run the tree's jichi exists; guard anyway -- a subset run that
# skipped `make` must not red on the probe, the line is a no-op.
[ -x "$BIN" ] && "$BIN" --version > "$tmp/binv" 2>/dev/null

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
# The neutralising, the main driver's form: ambient harness loader settings
# (env wins over the config) and the operator's ambient git config for the
# fixture repos.
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

# --- the WORKTREE (the i-3 shape: the tree's own head, self-contained) -------
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
    # an absence (def-350's in-stage rule). A make that exits 0 but built
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
        t_skip_one "2: the repin completes rc 0 under ASan -- SKIP: the toolchain cannot build the SAN subject"
        t_skip_one "3: the key delivery worked -- SKIP: the toolchain cannot build the SAN subject"
        t_done
    fi
    t_fail "1: the SAN build failed (rc=$SBRC; binary present: $([ -x "$SANBIN" ] && echo yes || echo no)) -- a committed regression cell that cannot build is evidence, not an absence; the make log's tail: $(tail -5 "$tmp/san.build.log" | tr '\n' '|' | head_bytes 300)"
    t_fail "2: the repin completes rc 0 under ASan (NOT RUN -- the build failed)"
    t_fail "3: the key delivery worked (NOT RUN -- the build failed)"
    t_done
fi

# --- the fixture: seed (2 commits) -> bare -> push.git clone + fetch ---------
SEED="$tmp/seed"; BARE="$tmp/bare.git"; PUSH="$tmp/push.git"
D="$tmp/drive"; CI="$tmp/ci"; SHIM="$tmp/shim"; CFG="$D/conf/harness.json"
KEY="$D/key"
RP_USER="Repin SAN Smoke"
RP_EMAIL="repin-san-smoke@example.com"
# The canary: NEVER a real key. Distinct 12-char prefix from the main
# driver's ("sanrepinCANA" vs "repinCANARY0"), its own string.
CANARY="sanrepinCANARY0123456789abcdef"
CANLEN=${#CANARY}

build_seed() {
    rm -rf "$SEED" "$BARE" "$PUSH"
    git init -q -b master "$SEED" || return 1
    git -C "$SEED" config user.email "$RP_EMAIL"
    git -C "$SEED" config user.name "$RP_USER"
    mkdir -p "$SEED/src/harness"
    echo A0 > "$SEED/src/harness/a.c"
    # The build stand-in writes the two artifacts into the clone; they must
    # be IGNORED or b9's porcelain refusal fires (the main driver's note).
    printf 'jichi\njichi-harness\n' > "$SEED/.gitignore"
    git -C "$SEED" add src .gitignore 2>/dev/null
    GIT_AUTHOR_DATE="2026-11-19T00:00:00Z" GIT_COMMITTER_DATE="2026-11-19T00:00:00Z" \
        git -C "$SEED" -c user.name="$RP_USER" -c user.email="$RP_EMAIL" commit -q -m C1
    C1=$(git -C "$SEED" rev-parse HEAD)
    # C2 changes ONE src/harness file vs C1 (the target S8).
    echo A2 > "$SEED/src/harness/a.c"
    git -C "$SEED" add src/harness/a.c 2>/dev/null
    GIT_AUTHOR_DATE="2026-11-19T00:00:01Z" GIT_COMMITTER_DATE="2026-11-19T00:00:01Z" \
        git -C "$SEED" -c user.name="$RP_USER" -c user.email="$RP_EMAIL" commit -q -m C2
    C2=$(git -C "$SEED" rev-parse HEAD)
    git init -q --bare "$BARE" || return 1
    git -C "$SEED" push -q "$BARE" "refs/heads/master:refs/heads/master" || return 1
    # file:// (NOT a plain path): a plain-path clone hard-links objects;
    # file:// fetches only the reachable refs (the main driver's idiom).
    git clone -q "file://$BARE" "$PUSH" || return 1
    # b3's convention: origin/master exists ONLY after an explicit fetch.
    git -C "$PUSH" fetch -q origin || return 1
    return 0
}

write_shims() {
    mkdir -p "$SHIM" || return 1
    # build-ok: the cwd is the per-sha clone; s8 = the clone dir's basename.
    # Both fixture binaries END their --version on the build line (the port's
    # measured shape; i-1's any-line neighbour is the main driver's cell 20).
    cat > "$SHIM/build-ok.sh" <<'EOS'
#!/bin/sh
s8=$(basename "$PWD")
mk() {
    printf '#!/bin/sh\nif [ "$1" = --version ]; then\n  echo "%s 0.14.1"\n  echo "smoke fixture binary"\n  echo "build: %s"\nfi\n' "$2" "$s8" > "./$1"
    chmod +x "./$1"
}
mk jichi jichi
mk jichi-harness jichi-harness
exit 0
EOS
    # hdoctor-ok: the clean summary, the port's own shape.
    printf '#!/bin/sh\nprintf "1 ok, 0 warnings, 0 problems\\n"\nexit 0\n' > "$SHIM/hdoctor-ok.sh"
    # live-argv: the i-3 subject + b20's teeth (the main driver's
    # live-argv.sh form). $1 = the model (the verb appends it, the <m>
    # token). Dumps its argv to a file, prints the JICHI_API_KEY LENGTH
    # from the env (the delivery line; the value itself never), then the
    # clean summary, exit 0. Under i-3 the child DIES in child_apply_env
    # before it prints anything -- the dump file is still written (it
    # precedes the abort), and the env line is what check 3 misses.
    cat > "$SHIM/live-argv.sh" <<'EOS'
#!/bin/sh
printf 'argv: ' > "@SHIM@/argv-dump.txt"
for a in "$@"; do printf '[%s] ' "$a" >> "@SHIM@/argv-dump.txt"; done
printf '\n' >> "@SHIM@/argv-dump.txt"
printf 'JICHI_API_KEY length=%s\n' "${#JICHI_API_KEY}"
printf '1 ok, 0 warnings, 0 problems\n'
exit 0
EOS
    sed 's|@SHIM@|'"$SHIM"'|g' "$SHIM/live-argv.sh" > "$SHIM/live-argv.sh.t" \
        && mv "$SHIM/live-argv.sh.t" "$SHIM/live-argv.sh"
    chmod +x "$SHIM"/*.sh 2>/dev/null
    return 0
}

build_drive() {
    mkdir -p "$D/bin" "$D/pins" "$D/pin-build" "$D/backups" "$D/conf" "$CI" || return 1
    printf '%s\n' "$CANARY" > "$KEY"; chmod 600 "$KEY"
    OLD8=$(printf '%.8s' "$C1")
    mkdir -p "$D/pins/$OLD8"
    printf 'old-pinned-jichi-%s\n' "$OLD8" > "$D/pins/$OLD8/jichi"
    printf 'old-pinned-harness-%s\n' "$OLD8" > "$D/pins/$OLD8/jichi-harness"
    cp "$D/pins/$OLD8/jichi" "$D/bin/jichi"
    cp "$D/pins/$OLD8/jichi-harness" "$D/bin/jichi-harness"
    ln -sfn "$OLD8" "$D/pins/current"
    # The jichi config the offline doctor references (the doctor is a
    # stand-in here, but the loader reads the field; keep it valid).
    printf '{"lowResource":false}\n' > "$D/conf/jichi-config.json"
    # The green `make ci` record for exactly S8 (b4: tail -1 ends rc=0).
    printf 'make ci rc=0\n' > "$CI/ci-$S8.log.meta"
    printf 'ci: OK\n' > "$CI/ci-$S8.log"
    # The repin config: the MAIN driver's form -- all six host paths named
    # EXPLICITLY (its cfg_build), NOT the defaults. i-3 is the subject of
    # this driver, and i-2 (the defaults' aliasing, the main driver's cell
    # 21) would REFUSE the run at `pins/current is not a link` -- before the
    # live-doctor child the i-3 check needs is ever spawned. Naming the six
    # paths keeps the run on the i-3 leg, the main driver's cells 1-19 form
    # (every repin there reaches the swap and the doctors; none of them can
    # see i-3, because they run the non-SAN binary).
    cat > "$CFG" <<EOF
{"dir":"$D","modelKeyFile":"$KEY","jichiConfig":"$D/conf/jichi-config.json",
 "host":{"dir":"$D","pushRepo":"$PUSH","ciLogDir":"$CI",
  "repinBuildDir":"$D/pin-build","pinsDir":"$D/pins","binDir":"$D/bin","backupDir":"$D/backups","repinOut":"$D/repin.out",
  "repinModels":"M1 M2","repinBuildRun":"$SHIM/build-ok.sh",
  "repinDoctor":"$SHIM/hdoctor-ok.sh","repinLiveDoctor":"$SHIM/live-argv.sh <m>"}}
EOF
    return 0
}

if ! build_seed || ! write_shims; then
    fixture_fail "the base git/drive fixture could not be built"
fi
# S8 BEFORE build_drive: the ci meta is written as ci-<s8>.log.meta (b4
# reads it by name). The main driver sets its S8 inside its build_drive;
# here the order is the fixture's, not the cell's.
S8=$(printf '%.8s' "$C2")
if ! build_drive; then
    fixture_fail "the drive dir could not be built"
fi

# --- 1: the SAN build is REAL (strings: __asan_ / LeakSanitizer) --------------
if strings "$SANBIN" 2>/dev/null | "$G" -qE '__asan_|LeakSanitizer'; then
    t_ok "1: the SAN build is real -- $SANBIN holds an __asan_/LeakSanitizer symbol (every later verdict is measured against a sanitized binary)"
else
    t_fail "1: the SAN build is NOT sanitized -- $SANBIN holds no __asan_/LeakSanitizer symbol (a make that exits 0 and built nothing, or a toolchain that accepted -fsanitize and silently ignored it); every later verdict is void, so the checks below fail"
fi

# --- 2: the repin completes rc 0 under ASan (NO report in either capture) -----
# The env-prefix form (stranded_call_uaf.sh: `env NAME=value cmd`, NOT an
# assignment prefix -- FreeBSD's /bin/sh would not export it). No timeout
# wrapper: the run is capped by nothing, and i-3's abort must surface as
# this driver's own verdict. The i-3 child abort lands in the CAPTURED
# live-doctor output (the verb's captured child: rc 1 -> NOT clean -> the
# run's rc 1), so the stdout capture is scanned; the stderr capture is
# scanned too (the parent's own ASan report, if the defect ever reaches it).
rm -f "$SHIM/argv-dump.txt"
env ASAN_OPTIONS=abort_on_error=0 "$SANBIN" --config "$CFG" repin "$S8" \
    < /dev/null > "$tmp/san.out" 2> "$tmp/san.err"
RC2=$?
AS_IN_OUT=0; "$G" -qF 'ERROR: AddressSanitizer' "$tmp/san.out" && AS_IN_OUT=1
AS_IN_ERR=0; "$G" -qF 'ERROR: AddressSanitizer' "$tmp/san.err" && AS_IN_ERR=1
UA_IN_OUT=0; "$G" -qF 'stack-use-after-scope' "$tmp/san.out" && UA_IN_OUT=1
UA_IN_ERR=0; "$G" -qF 'stack-use-after-scope' "$tmp/san.err" && UA_IN_ERR=1
if [ "$RC2" -eq 0 ] && [ "$AS_IN_OUT" -eq 0 ] && [ "$AS_IN_ERR" -eq 0 ] \
   && [ "$UA_IN_OUT" -eq 0 ] && [ "$UA_IN_ERR" -eq 0 ] \
   && "$G" -qF "repin: done $S8, doctors clean" "$tmp/san.out"; then
    t_ok "2: the repin completes rc 0 under ASan -- NEITHER the captured stdout NOR the stderr holds an ASan report signature ('ERROR: ' plus the sanitizer name, or 'stack-use-after-scope'), and the last line is 'repin: done $S8, doctors clean' (the live-doctor child with the key ran to completion un-poisoned; i-3's env slot is live across the fork)"
else
    t_fail "2: the repin under ASan expected rc 0 + no 'ERROR: AddressSanitizer' / 'stack-use-after-scope' in either capture (got rc=$RC2; asan-stdout=$AS_IN_OUT asan-stderr=$AS_IN_ERR uas-stdout=$UA_IN_OUT uas-stderr=$UA_IN_ERR; the report's head: $(tr '\n' '|' < "$tmp/san.out" | "$G" -m 1 'AddressSanitizer' | head_bytes 240) / $(tr '\n' '|' < "$tmp/san.err" | "$G" -m 1 'AddressSanitizer' | head_bytes 240); last stdout: $(tail -1 "$tmp/san.out" 2>/dev/null | head_bytes 120))"
fi

# --- 3: the key delivery worked (the argv dump holds NO canary; the env line
#        IS in the captured output -- i-3's abort must not be masked as a
#        missing env: under the defect the child dies in child_apply_env
#        BEFORE it prints its env line, so this check fails there too). -----
DUMP_OK=0; [ -f "$SHIM/argv-dump.txt" ] && DUMP_OK=1
CAN_IN_DUMP=0; [ -f "$SHIM/argv-dump.txt" ] && "$G" -qF "$CANARY" "$SHIM/argv-dump.txt" && CAN_IN_DUMP=1
LEN_IN_OUT=0; "$G" -qF "JICHI_API_KEY length=$CANLEN" "$tmp/san.out" && LEN_IN_OUT=1
if [ "$DUMP_OK" -eq 1 ] && [ "$CAN_IN_DUMP" -eq 0 ] && [ "$LEN_IN_OUT" -eq 1 ]; then
    t_ok "3: the key delivery worked -- the live-doctor stand-in's argv dump holds NO canary substring (the key is never on argv, b20's teeth) and the captured output holds 'JICHI_API_KEY length=$CANLEN' (the env delivery reached the child: under i-3 the child would have died in child_apply_env before printing this line, so this check fails there too -- the abort is not masked as a missing env)"
else
    t_fail "3: the key delivery expected a canary-free argv dump + 'JICHI_API_KEY length=$CANLEN' in the captured output (got dump-present=$DUMP_OK canary-in-dump=$CAN_IN_DUMP env-line-in-out=$LEN_IN_OUT; dump: $(head_bytes 200 "$SHIM/argv-dump.txt" 2>/dev/null | tr '\n' '|'); out head: $(tr '\n' '|' < "$tmp/san.out" | head_bytes 200))"
fi

t_done
