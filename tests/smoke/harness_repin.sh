#!/bin/sh
# smoke: `jichi-harness --config <f> repin <SHA8>` -- the re-pin (M1203, batch
# H11; the C89 port of the 2026-10-27 hardening-host drive's repin.sh, 103
# lines). RED FIRST: the verb does not exist on this head -- `jichi-harness`
# answers every `repin` invocation with `jichi-harness: unknown command
# 'repin'` (src/harness/jc_harness_main.c:550, stderr) and exit 2, so every
# cell below is not ok. That is the expected wave-1 state; wave 2 lands
# src/harness/jc_harness_repin.c and makes the driver green. The assertions
# here ARE the port's contract (notes/M1203.md, the b1-b24 behaviour list and
# the 19 red-first cells), written now.
#
# WHAT IS CHECKED. Every refusal string is BYTE-EXACT from the reference
# (docs/internal/drive/2026-10-27-hardening-host/repin.sh): the supervisor's
# switch-over diffs the two outputs, so a changed byte is a diff that must be
# explained as a CHANGED line, never discovered. Refusals and usage are
# asserted on stderr and the say()/summary lines on stdout, the sibling `land`
# port's convention (jc_harness_land.c: die -> stderr, printf -> stdout).
#
#   1  b1   usage: no <SHA8> -> rc 2, stderr names `usage: jichi-harness repin <SHA8>`.
#   2  b2   sha not in push.git -> `repin: <C> is not a commit in push.git`, rc 1,
#          NO per-sha clone dir made (the refusal precedes b7: the order proof).
#   3  b3a  push.git without refs/remotes/origin/master -> `repin: no origin/master in push.git`.
#   4  b3   sha != fetched master (a commit BEHIND master) -> `repin: refused: <s8> is not
#          GitLab master as push.git last fetched it (<m8>)` -- the word GitLab stays.
#   5  b4   ci meta last line ` rc=1` -> `repin: refused: no green make ci record for <s8>
#          (<meta>)` (the path named); meta ABSENT -> same; ` rc=0` mid-file, not last -> same.
#   6  b5a  pins/current absent / a plain file -> `repin: pins/current is not a link`.
#   7  b5b  pins/current -> <s8> already -> `repin: refused: pins/current is <s8> already`.
#   8  b7   per-sha clone dir pre-created -> `repin: refused: <B> exists`; repinClone
#          shim exit 1 -> `repin: the clone failed`; the DEFAULT-clone control (no
#          shim, its own single cfg_build) asserts only the reference's
#          clone+checkout (repin.sh:49): pin-build/<s8> exists with a checked-out
#          tree (HEAD=<C2>) and NO `the clone failed` line -- the run may refuse or
#          succeed LATER; the arm does not assert the run's rc (each arm has its own
#          single cfg_build: the double build that reset CFG_CLONE before the control
#          arm, making 8b and 8c run identical state needing opposite outcomes, is
#          gone).
#   9  b8   build stand-in exit 1 -> `repin: the build failed: <log path>` (the path
#          named, the log exists with the stand-in's stderr).
#   10 b9   build stand-in leaves a porcelain file -> `repin: the build left 1
#          porcelain line(s) in <clone>`.
#   11 b10  jichi stamp `build: <OTHER>` -> `repin: jichi --version does not say
#          build: <s8>`; jichi-harness stamp missing -> its named refusal; a `(dirty)`
#          suffix -> refused (the grep -qx whole-line form).
#   12 b11  pins/<s8> pre-created -> `repin: refused: pins/<s8> exists`.
#   13 b12  two runs. Run A: the running bin/ is TAMPERED FIRST, then repin S8 --
#          the reference creates pins/<s8> BEFORE the backup (repin.sh:56-57 vs
#          :59-63), so the run leaves pins/<s8> PRESENT; the backup is taken from
#          the corrupted bin/ (repin.sh:59-60, bin/<old>), then the b12 cmp
#          (repin.sh:61-63) is BACKUP vs PINS/<old> -- NOT backup vs the running
#          bin/ -- so the tampered backup differs from pins/<old>/jichi -> rc 1,
#          `repin: the backup <bk>/jichi differs from pins/<old>/jichi: stop
#          before the swap`, and NO swap (current still <old>, bin/ still the
#          tampered bytes); the backup EXISTS holding the tampered bytes
#          (provenance: the snapshot came from the corrupted bin/ -- exactly why
#          the repin stopped rather than rolled forward). Inter-run cleanup (the
#          driver's state; the verb NEVER rms): remove pin-build/<s8> + its log +
#          pins/<s8>, KEEP the backup -- that is the point. Run B: set_master C3,
#          tamper the running bin/ with DIFFERENT bytes, repin S9 (a DIFFERENT
#          target so b7's pin-build/<s9> is fresh; the backup still keys to
#          bin-<old> since current is <old>): the cmp of the (still run-A bytes)
#          backup against pins/<old>/jichi still fails -> the SAME named refusal,
#          NO swap; the backup is NOT rewritten (still holds the run-A bytes, NOT
#          the run-B bytes).
#   14 b13/b14/b15 SUCCESS -> rc 0; bin/ == pins/<s8> (cmp both); pins/current a LINK
#          reading <s8>; the backup's provenance by cmp: after repin 1 (OLD8->S8),
#          bin-<old>/jichi == pins/<old>/jichi (the snapshot came from the clean
#          running pair); after repin 2 (S8->S9), bin-<s8>/jichi == pins/<s8>/jichi
#          (the snapshot came from the just-swapped bin/ == pins/<s8>); the summary
#          lines present (b6 first -- the out file's FIRST line is the b6
#          summary itself, exact line + a negative control: it is NO other
#          say line; b15 the swap line, b22 the last `repin: done
#          <s8>, doctors clean`);
#          NO dir or file the run made is deleted (b16: clone dir, both pins dirs, the
#          backup, the out file exist after rc 0); the out file (b23) carries the STAMPED
#          lines, a second repin's lines appended after the first run's (grows, never
#          truncates). The doctors are REAL: the offline harness doctor (the port's own
#          `jichi-harness doctor`) and `jichi doctor --live` against tests/tools/mockmodel
#          (the measured green recipe: a .mm with match "jichi_probe_echo" -> a
#          NON-streamed body carrying the jichi_probe_echo tool call + a `text ok`
#          fallback, max-requests 10; the doctor makes ~4 requests, 2 was too few).
#   15 b17  a fixture pair differing in 2 src/harness files -> the line names `: 2`; a
#          pair with 0 diff -> `: 0` (the loop-restart measurement, both ways). Run B
#          targets S9: the driver first removes cell 14's leftover pin-build/<s9> +
#          pins/<s9> (b16 binds the VERB, not the fixture); current=S8 and
#          bin/==pins/<s8> come from run A's own swap.
#   16 b19  the harness-doctor stand-in's summary `1 ok, 0 warnings, 1 problems` -> rc 1,
#          `repin: done <s8>, doctors NOT clean`, the swap HAD happened (the doctors run
#          AFTER the swap: a not-clean doctor does not un-swap).
#   17 b20  the key-never-on-argv teeth: the stand-in's argv dump holds NO canary; its
#          env-length probe prints the key's length (the env delivery worked). The
#          config resets the harness-doctor stand-in to CLEAN (cell 16 left it
#          not-clean; rc 0 needs it) -- the state matches the comment.
#   18 b21  the live-doctor stand-in prints a line holding the canary's 12-char PREFIX
#          (the teeth unit b21 guards -- the planted leak holds ONLY the prefix, which is
#          NOT a substring of the full canary, so the census must grep the PREFIX too,
#          not just the full canary) ->
#          `repin: doctor --live <mdl>: output WITHHELD (it holds the key's prefix)`,
#          rc 1, the doctor-live-<mdl> file NOT written, and NEITHER the canary NOR its
#          12-char prefix in NO file under the verb's output surfaces (find +
#          /usr/bin/grep, the config_credentials.sh census form, both scans empty); the
#          SECOND model (clean) still ran and its file exists (the continue, not abort).
#   19 b22  the census: every refusal rc 1, usage rc 2, success + clean rc 0,
#          success + not-clean rc 1 (one cell over the recorded rcs).
#   20 i-1  b10's ANY-line form (M1203-i, the reference's `grep -qx` reads ANY
#          whole line, repin.sh:54 -- not the last one): arm (a) a build stand-in
#          whose `jichi --version` prints the REAL measured shape since M1133
#          (`build: <s8>` on a MIDDLE line, then `libcurl: ...` last) and whose
#          `jichi-harness --version` ends on its build line -> the repin COMPLETES
#          rc 0 (success: bin/ == pins/$S8, current -> $S8, `done $S8, doctors
#          clean`); every stand-in in cells 1-19 ends on its build line, so this
#          neighbour is what they cannot see. Arm (b) the same shape with
#          `build: <s8> (dirty)` on the middle line -> STILL refused, rc 1, the
#          byte-exact `repin: jichi --version does not say build: <s8>` on stderr
#          (the whole-line form kept: a suffix on a middle line is not a clean
#          whole line).
#   21 i-2  the six `<host.dir>/...` defaults (M1203-i, the reference's shape:
#          repin.sh hard-codes $H/push.git, $H/pin-build, pins/, $D/bin,
#          $D/backups, $H/repin.out from its own two dirs; every path below comes
#          from a default). Arm (a) a config naming ONLY host.dir/pushRepo/
#          ciLogDir + repinModels + the shimmable commands (NO repinBuildDir/
#          pinsDir/binDir/backupDir/repinOut) -> rc 0, and the run's own
#          artefacts prove each default resolved to its OWN path: pin-build/$S8/
#          is a DIR (not under a repin.out FILE), bin/jichi == pins/$S8/jichi
#          (cmp: the swap happened at the defaulted bin/pins), backups/bin-<old>/
#          exists (the snapshot at the defaulted backup), repin.out is a FILE
#          that grew. Arm (b) a config naming everything EXCEPT repinOut (one
#          single-defaulted field among named ones) -> rc 0 + the default out
#          file grew (the transitive aliasing there too).
#   22 i-4  the child deadline (M1203-i, SV:1241: the reference's sh -c + make -j8
#          have NO deadline; the port's single jc_proc_capture passed 0L, which
#          include/jc_proc.h resolved to the UNMEASURED 30 s JC_PROC_DEF_TIMEOUT
#          kill): a build stand-in that SLEEPS 35 s (past the old 30 s default,
#          comfortably under the fix's named 600 s bound) BEFORE writing the two
#          FINAL-line-shape --version binaries (the build-ok.sh shape) must NOT be
#          killed by the verb -- the repin COMPLETES rc 0 with `done $S8, doctors
#          clean` and pins/current -> $S8 (the child finished, the swap happened).
#          Unfixed the build child's group is SIGKILL'd at the 30 s deadline
#          (jc_proc_capture returns -2, rr_run_cmd's failure path) and the
#          repin refuses rc 1 at the build leg with the byte-exact cell-9 line
#          `repin: the build failed: <log>` (measured: the step-1 red run, wall
#          ~30 s, and the persistence revert-to-0L run). The kill lands
#          mid-sleep, so the stand-in never writes the binaries and the log
#          stays empty -- the build leg still names it (a -2 is a refusal, not
#          a pass). The negative arm
#          (a child out-running the NAMED 600 s bound) is NOT in this driver: it
#          would out-run run.sh's own deadline and manufacture a false red -- it
#          is pinned in code (rr_run_cmd names the deadline in its failure path)
#          and in the operator's deadline API (SV:1249, the gate-port card).
#          The one 35 s sleep is why this driver sits on run.sh's 600-s list.
#   23 i-5  the exec-bit contract (M1203-i5, SV:0912, card [131]): the three
#          copies (pins, backup, the .new swap, repin.sh:57/:60/:65) keep the
#          source's exec bit; the swapped jichi-harness runs (`--version` exits 0
#          and output carries `build: <s8>`). Cell 23 runs LAST (after cell 22's
#          reset_clean), is self-contained (it sets its own fixture state and ends
#          with reset_clean), and is red at the unfixed rr_cp (the three copies
#          take 0644 from fopen "wb", not the sources' 0755). GREEN after the fix:
#          23a all copies are -x; 23b rc 0 + `build: <s8>`.
#
# FIXTURE (real git under $tmp, the land driver's form): a seed repo with 4
# commits at FIXED dates + -c user.name/-c user.email (the chain's shas are
# deterministic): C0, C1 (the OLD pin), C2 = S8 (the repin target; it changes
# TWO files under src/harness/ vs C1 -> b17 `: 2`), C3 = S9 (ZERO src/harness
# files vs C2 -> b17 `: 0`). The seed carries a .gitignore for the two build
# artifacts, so a clean build stand-in leaves 0 porcelain lines and the
# cell-10 stand-in's extra file is exactly 1. A local BARE repo (bare.git) +
# push.git, a clone of it whose refs/remotes/origin/master is set by an
# explicit `git fetch` (b3's convention: the ref exists only after a fetch);
# cell 3 uses a second clone with the ref deleted. A drive dir D with bin/
# (the running pair), pins/<old>/ + a pins/current symlink, pin-build/,
# backups/, repin.out, conf/, home/, and a key file holding a CANARY (never a
# real key). A ci log dir with fabricated ci-<s8>.log.meta (last line ` rc=0`
# in the base; the cell-5 arms rewrite it). The shimmable CHANGED fields
# (b7-b23) are named by the harness config; the shims are self-contained
# scripts under $tmp/shim, written token-substituted so no shell state leaks
# in. In the RED state the verb is absent, so no shim is ever invoked and
# mockmodel is never started: the cells go red on the unknown-command line.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"

t_plan 23
smoke_home
tmp=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# Build the harness binary once, like the sweep's neighbours (a hollow binary
# makes every cell not ok, per the sweep's hollowing rule).
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

# A fixture failure emits one t_fail per cell so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt 23 ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

# --- identity / secrets --------------------------------------------------------
RP_USER="Repin Smoke"
RP_EMAIL="repin-smoke@example.com"
# The canary: a token that is NEVER a real key. Its first 12 chars
# ("repinCANARY0") are the b21 prefix the withholding test matches.
CANARY="repinCANARY0123456789abcdef0123456789"
CANPFX=$(printf '%s' "$CANARY" | cut -c1-12)
CANLEN=${#CANARY}

# --- paths ---------------------------------------------------------------------
SEED="$tmp/seed"; BARE="$tmp/bare.git"; PUSH="$tmp/push.git"; PUSH2="$tmp/push2.git"
D="$tmp/drive"; CI="$tmp/ci"; SHIM="$tmp/shim"; CFG="$D/conf/harness.json"
KEY="$D/key"

# --- the seed chain (fixed dates + -c user -> deterministic shas) ---------------
rp_commit() { # rp_commit MSG DATE -- writes the date into $SEED/$RP_FILE, commits, echoes sha
    echo "$2" > "$SEED/$RP_FILE"
    git -C "$SEED" add "$RP_FILE" 2>/dev/null
    GIT_AUTHOR_DATE="$2" GIT_COMMITTER_DATE="$2" \
        git -C "$SEED" -c user.name="$RP_USER" -c user.email="$RP_EMAIL" \
        commit -q -m "$1"
    git -C "$SEED" rev-parse HEAD
}
build_seed() {
    rm -rf "$SEED" "$BARE" "$PUSH" "$PUSH2"
    git init -q -b master "$SEED" || return 1
    git -C "$SEED" config user.email "$RP_EMAIL"
    git -C "$SEED" config user.name "$RP_USER"
    mkdir -p "$SEED/src/harness"
    echo A0 > "$SEED/src/harness/a.c"; echo B0 > "$SEED/src/harness/b.c"
    echo O0 > "$SEED/src/other.c"
    # The build stand-in writes the two artifacts into the clone; they must be
    # IGNORED or the success build leaves 2 porcelain lines and b9 refuses.
    printf 'jichi\njichi-harness\n' > "$SEED/.gitignore"
    git -C "$SEED" add src .gitignore 2>/dev/null
    GIT_AUTHOR_DATE="2026-11-09T00:00:00Z" GIT_COMMITTER_DATE="2026-11-09T00:00:00Z" \
        git -C "$SEED" -c user.name="$RP_USER" -c user.email="$RP_EMAIL" commit -q -m C0
    C0=$(git -C "$SEED" rev-parse HEAD)
    RP_FILE=src/harness/a.c; C1=$(rp_commit C1 "2026-11-09T00:00:01Z") || return 1
    # C2 changes TWO src/harness files vs C1 (b17 `: 2`): a.c here, b.c next.
    RP_FILE=src/harness/a.c; C2=$(rp_commit C2a "2026-11-09T00:00:02Z") || return 1
    RP_FILE=src/harness/b.c; C2=$(rp_commit C2b "2026-11-09T00:00:03Z") || return 1
    # C3 changes ZERO src/harness files vs C2 (b17 `: 0`).
    RP_FILE=src/other.c;    C3=$(rp_commit C3 "2026-11-09T00:00:04Z") || return 1
    # The bare + the push clone. file:// (NOT a plain path): a plain-path clone
    # hard-links objects; file:// fetches only the reachable refs (land idiom).
    git init -q --bare "$BARE" || return 1
    git -C "$SEED" push -q "$BARE" "refs/heads/master:refs/heads/master" || return 1
    git -C "$BARE" update-ref refs/heads/master "$C2"
    git clone -q "file://$BARE" "$PUSH" || return 1
    # b3's convention: origin/master exists ONLY after an explicit fetch.
    git -C "$PUSH" fetch -q origin || return 1
    return 0
}

# --- the shims (written once; token-substituted so no shell state leaks) --------
# A quoted heredoc + sed token pass: the shim's OWN shell variables stay
# literal ($1, $PWD, ${#JICHI_API_KEY}), the fixture values are baked in.
write_shims() {
    mkdir -p "$SHIM" || return 1
    # build-ok: the cwd is the per-sha clone; s8 = the clone dir's basename.
    # Writes two fixture "binaries" that print a 3-line --version with
    # `build: <s8>` on line 3 (the pinned binary's measured shape).
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
    # build-fail: exit 1 with stderr (cell 9: the log carries it, b8 names it).
    printf '#!/bin/sh\necho "build-broken-shim" >&2\nexit 1\n' > "$SHIM/build-fail.sh"
    # build-porcelain: leave exactly ONE untracked (non-ignored) file (cell 10).
    printf '#!/bin/sh\n: > ./porc.txt\nexit 0\n' > "$SHIM/build-porcelain.sh"
    # build-badstamp: jichi says `build: other123`, jichi-harness absent (cell 11).
    cat > "$SHIM/build-badstamp.sh" <<'EOS'
#!/bin/sh
printf '#!/bin/sh\nif [ "$1" = --version ]; then\n  echo "jichi 0.14.1"\n  echo "smoke fixture binary"\n  echo "build: other123"\nfi\n' > ./jichi
chmod +x ./jichi
exit 0
EOS
    # build-badstamp2: jichi correct, jichi-harness says `build: other123`.
    cat > "$SHIM/build-badstamp2.sh" <<'EOS'
#!/bin/sh
s8=$(basename "$PWD")
printf '#!/bin/sh\nif [ "$1" = --version ]; then\n  echo "jichi 0.14.1"\n  echo "smoke fixture binary"\n  echo "build: %s"\nfi\n' "$s8" > ./jichi
chmod +x ./jichi
printf '#!/bin/sh\nif [ "$1" = --version ]; then\n  echo "jichi-harness 0.14.1"\n  echo "smoke fixture binary"\n  echo "build: other123"\nfi\n' > ./jichi-harness
chmod +x ./jichi-harness
exit 0
EOS
    # build-dirty: both stamps carry a `(dirty)` suffix (cell 11, the whole-line arm).
    cat > "$SHIM/build-dirty.sh" <<'EOS'
#!/bin/sh
s8=$(basename "$PWD")
for b in jichi jichi-harness; do
    printf '#!/bin/sh\nif [ "$1" = --version ]; then\n  echo "%s 0.14.1"\n  echo "smoke fixture binary"\n  echo "build: %s (dirty)"\nfi\n' "$b" "$s8" > "./$b"
    chmod +x "./$b"
done
exit 0
EOS
    # build-realshape (cell 20, i-1's any-line arm): the REAL measured shape of
    # `jichi --version` since M1133 -- FOUR lines, `build: <s8>` on a MIDDLE
    # line, then `libcurl: ...` LAST (the pinned binary's own stamp); every
    # stand-in in cells 1-19 ends on its build line, so this is the neighbour
    # they cannot see. `jichi-harness --version` is the three-line form that
    # DOES end on its build line (harness's measured shape today). Both must
    # pass b10 (the reference's `grep -qx "build: $s"`: ANY whole line).
    cat > "$SHIM/build-realshape.sh" <<'EOS'
#!/bin/sh
s8=$(basename "$PWD")
printf '#!/bin/sh\nif [ "$1" = --version ]; then\n  echo "jichi 0.14.1"\n  echo "smoke fixture binary"\n  echo "build: %s"\n  echo "libcurl: 8.18.0 (TLS: OpenSSL/3.5.5)"\nfi\n' "$s8" > ./jichi
chmod +x ./jichi
printf '#!/bin/sh\nif [ "$1" = --version ]; then\n  echo "jichi-harness 0.14.1"\n  echo "smoke fixture binary"\n  echo "build: %s"\nfi\n' "$s8" > ./jichi-harness
chmod +x ./jichi-harness
exit 0
EOS
    # build-slow (cell 22, i-4's deadline teeth): the shim script itself SLEEPS
    # 35 s BEFORE writing the binaries (the build-ok.sh three-line FINAL-line
    # shape), then exits 0. The sleep out-runs the OLD unmeasured 30 s
    # JC_PROC_DEF_TIMEOUT kill the unfixed rr_run_cmd's `0L` resolved to (the
    # reference's sh -c/make -j8 have no deadline) and stays comfortably under
    # the fix's named 600 s bound, so the unfixed red arrives at wall ~30 s and
    # the fixed green at wall ~35 s -- this cell is the one that puts the driver
    # on run.sh's 600-s list (not ~10 min).
    cat > "$SHIM/build-slow.sh" <<'EOS'
#!/bin/sh
sleep 35
s8=$(basename "$PWD")
mk() {
    printf '#!/bin/sh\nif [ "$1" = --version ]; then\n  echo "%s 0.14.1"\n  echo "smoke fixture binary"\n  echo "build: %s"\nfi\n' "$2" "$s8" > "./$1"
    chmod +x "./$1"
}
mk jichi jichi
mk jichi-harness jichi-harness
exit 0
EOS
    # build-realdirty (cell 20b, i-1's negative arm): the same real four-line
    # shape with `build: <s8> (dirty)` on the middle line -- the WHOLE-line form
    # must STILL refuse (a suffix on a middle line is not a clean whole line;
    # the reference's grep -qx reads any line, and none of them equals
    # `build: <s8>`).
    cat > "$SHIM/build-realdirty.sh" <<'EOS'
#!/bin/sh
s8=$(basename "$PWD")
printf '#!/bin/sh\nif [ "$1" = --version ]; then\n  echo "jichi 0.14.1"\n  echo "smoke fixture binary"\n  echo "build: %s (dirty)"\n  echo "libcurl: 8.18.0 (TLS: OpenSSL/3.5.5)"\nfi\n' "$s8" > ./jichi
chmod +x ./jichi
printf '#!/bin/sh\nif [ "$1" = --version ]; then\n  echo "jichi-harness 0.14.1"\n  echo "smoke fixture binary"\n  echo "build: %s"\nfi\n' "$s8" > ./jichi-harness
chmod +x ./jichi-harness
exit 0
EOS
    # hdoctor-ok: the port's own OFFLINE doctor on the repin config (cell 14's
    # real harness-doctor leg; also the fast clean leg of the other cells).
    cat > "$SHIM/hdoctor-ok.sh" <<'EOS'
#!/bin/sh
exec "@HBIN@" --config "@CFG@" doctor < /dev/null
EOS
    sed 's|@HBIN@|'"$HBIN"'|; s|@CFG@|'"$CFG"'|' "$SHIM/hdoctor-ok.sh" > "$SHIM/hdoctor-ok.sh.t" \
        && mv "$SHIM/hdoctor-ok.sh.t" "$SHIM/hdoctor-ok.sh"
    # hdoctor-notclean: clean rc 0, NOT-clean summary (cell 16, b19).
    printf '#!/bin/sh\nprintf "1 ok, 0 warnings, 1 problems\\n"\nexit 0\n' > "$SHIM/hdoctor-notclean.sh"
    # live-ok: the fast clean per-model stand-in (the note's default shape).
    printf '#!/bin/sh\nprintf "1 ok, 0 warnings, 0 problems\\n"\nexit 0\n' > "$SHIM/live-ok.sh"
    # live-real: the SUCCESS cell's REAL `jichi doctor --live` against mockmodel
    # (the measured green recipe). $1 = the model name (the verb substitutes
    # the <m> token). Starts mockmodel, writes a per-model jichi config with
    # the canary apiKey + the mock port, runs the doctor, exits with its rc.
    cat > "$SHIM/live-real.sh" <<'EOS'
#!/bin/sh
m="$1"
portf="@SHIM@/live-real.port"; rm -f "$portf"
cat > "@SHIM@/live-real.mm" <<'MMM'
wire openai
rule
  match "jichi_probe_echo"
  status 200
  body {"choices":[{"index":0,"message":{"role":"assistant","tool_calls":[{"id":"1","type":"function","function":{"name":"jichi_probe_echo","arguments":"{\"text\": \"ping\"}"}}]}}]}
rule
  text ok
MMM
"@TOOLS@/mockmodel" --script "@SHIM@/live-real.mm" --capture "@SHIM@/live-real-cap" \
    --port-file "$portf" --deadline 60 --max-requests 10 >/dev/null 2>&1 &
mppid=$!
i=0; while [ ! -s "$portf" ]; do
    kill -0 "$mppid" 2>/dev/null || break
    i=$((i + 1)); [ "$i" -gt 12 ] && break
    sleep 1
done
port=$(cat "$portf" 2>/dev/null)
if [ -z "$port" ]; then
    kill "$mppid" 2>/dev/null
    printf '0 ok, 0 warnings, 1 problems\n'
    exit 1
fi
cfgj="@D@/conf/live-$m.json"
printf '{"toolProfile":"full","lowResource":false,"models":[{"name":"lead","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:%s/v1","apiKey":"%s","roles":["chat"]}],"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}' "$port" "@CANARY@" > "$cfgj"
"@BIN@" --config "$cfgj" --model lead doctor --live < /dev/null 2>&1
rc=$?
kill "$mppid" 2>/dev/null; wait "$mppid" 2>/dev/null
exit $rc
EOS
    sed 's|@SHIM@|'"$SHIM"'|g; s|@TOOLS@|'"$SMOKE_TOOLS"'|; s|@D@|'"$D"'|; s|@CANARY@|'"$CANARY"'|; s|@BIN@|'"$BIN"'|' \
        "$SHIM/live-real.sh" > "$SHIM/live-real.sh.t" \
        && mv "$SHIM/live-real.sh.t" "$SHIM/live-real.sh"
    # live-argv: the key-never-on-argv teeth (cell 17). $1 = model. Dumps its
    # argv to a file and prints the JICHI_API_KEY's LENGTH from the env.
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
    # live-canary: the withholding teeth (cell 18). $1 = model. M1 prints a
    # line holding the canary's 12-char prefix (WITHHELD); M2 prints clean.
    cat > "$SHIM/live-canary.sh" <<'EOS'
#!/bin/sh
m="$1"
if [ "$m" = "M1" ]; then
    printf 'leaked line holds @CANPFX@ -- must be withheld\n'
fi
printf 'clean live output for %s\n' "$m"
printf '1 ok, 0 warnings, 0 problems\n'
exit 0
EOS
    sed 's|@CANPFX@|'"$CANPFX"'|' "$SHIM/live-canary.sh" > "$SHIM/live-canary.sh.t" \
        && mv "$SHIM/live-canary.sh.t" "$SHIM/live-canary.sh"
    # clone-fail: the repinClone shim exiting 1 (cell 8b, b7's `the clone failed`).
    printf '#!/bin/sh\nexit 1\n' > "$SHIM/clone-fail.sh"
    chmod +x "$SHIM"/*.sh 2>/dev/null
    return 0
}

# --- the drive dir D (bin / pins / backups / conf / home / key / ci) -----------
build_drive() {
    mkdir -p "$D/bin" "$D/pins" "$D/pin-build" "$D/backups" "$D/conf" "$D/home" "$CI" || return 1
    printf '%s\n' "$CANARY" > "$KEY"; chmod 600 "$KEY"
    OLD8=$(printf '%.8s' "$C1")
    mkdir -p "$D/pins/$OLD8"
    printf 'old-pinned-jichi-%s\n' "$OLD8" > "$D/pins/$OLD8/jichi"
    printf 'old-pinned-harness-%s\n' "$OLD8" > "$D/pins/$OLD8/jichi-harness"
    cp "$D/pins/$OLD8/jichi" "$D/bin/jichi"
    cp "$D/pins/$OLD8/jichi-harness" "$D/bin/jichi-harness"
    ln -sfn "$OLD8" "$D/pins/current"
    # The jichi configs the offline doctor + the live stand-in reference. The
    # repin config (harness.json) is written per-cell by cfg_build.
    printf '{"lowResource":false}\n' > "$D/conf/jichi-config.json"
    printf 'home-fixture\n' > "$D/home/jichi.json"
    : > "$D/repin.out"
    # The base ci record for S8 (last line ` rc=0`) and for S9.
    S8=$(printf '%.8s' "$C2"); S9=$(printf '%.8s' "$C3")
    printf 'make ci rc=0\n' > "$CI/ci-$S8.log.meta"
    printf 'ci: OK\n' > "$CI/ci-$S8.log"
    printf 'make ci rc=0\n' > "$CI/ci-$S9.log.meta"
    printf 'ci: OK\n' > "$CI/ci-$S9.log"
    return 0
}

if ! build_seed || ! write_shims || ! build_drive; then
    fixture_fail "the base git/drive fixture could not be built"
fi

# --- resolved shas (the two repin targets + the old pin) -----------------------
S8=$(printf '%.8s' "$C2")   # C2 changes TWO src/harness files vs C1 -> b17 `: 2`
S9=$(printf '%.8s' "$C3")   # C3 changes ZERO src/harness files vs C2 -> b17 `: 0`
OLD8=$(printf '%.8s' "$C1")

# --- the harness config builder (the shimmable CHANGED fields, b7-b23) ---------
# Reads globals CFG_PUSH/CFG_CI/CFG_BUILD/CFG_CLONE/CFG_DOC/CFG_LIVE/CFG_MODELS/
# CFG_OUT/CFG_PINS/CFG_BIN/CFG_BK and writes a valid repin config to $1. The
# loader is lenient: it reads dir/modelKeyFile/gitTokenFile/jichiConfig (so the
# offline doctor is clean) and ignores the host.* repin fields until wave 2.
cfg_build() {
    cat > "$1" <<EOF
{"dir":"$D","modelKeyFile":"$KEY","gitTokenFile":"$KEY","jichiConfig":"$D/conf/jichi-config.json",
 "host":{"dir":"$D","pushRepo":"$CFG_PUSH","ciLogDir":"$CFG_CI",
  "repinBuildDir":"$D/pin-build","pinsDir":"$CFG_PINS","binDir":"$CFG_BIN","backupDir":"$CFG_BK",
  "repinOut":"$CFG_OUT","repinModels":"$CFG_MODELS","repinBuildRun":"$CFG_BUILD",
  "repinClone":"$CFG_CLONE","repinDoctor":"$CFG_DOC","repinLiveDoctor":"$CFG_LIVE <m>"}}
EOF
}
# Default globals (overridden per cell): the happy-path stand-ins.
CFG_PUSH="$PUSH"; CFG_CI="$CI"; CFG_BUILD="$SHIM/build-ok.sh"; CFG_CLONE=""
CFG_DOC="$SHIM/hdoctor-ok.sh"; CFG_LIVE="$SHIM/live-ok.sh"; CFG_MODELS="M1 M2"
CFG_OUT="$D/repin.out"; CFG_PINS="$D/pins"; CFG_BIN="$D/bin"; CFG_BK="$D/backups"

# --- helpers -------------------------------------------------------------------
# set_master SHA -- point push.git's refs/remotes/origin/master at SHA. b3 reads
# the LAST FETCHED value, so re-point the bare + fetch (a read of the bare is not
# "as push.git last fetched it"). ALSO push.git's refs/heads/master: the reference's
# plain clone + `checkout -q --detach <full>` (repin.sh:46-49) needs the target
# reachable from a refs/heads ref of the clone's origin -- the real harness MIRROR
# carries the landed commit under refs/heads (master) + refs/remotes/origin/master;
# this fixture's push.git is the stand-in for that mirror, so it must track the same
# way (refs/heads/master updated in place: a file:// clone hard-links objects, so the
# target is present in the odb). refs/remotes/origin/master stays (the b3 convention
# reads the latter).
set_master() {
    git -C "$BARE" update-ref refs/heads/master "$1" 2>/dev/null
    git -C "$PUSH" fetch -q origin 2>/dev/null
    git -C "$PUSH" update-ref refs/remotes/origin/master "$1" 2>/dev/null
    git -C "$PUSH" update-ref refs/heads/master "$1" 2>/dev/null
}
# reset_clean TARGET_FULL TARGET_S8 -- a known clean start for a repin of
# OLD8 -> TARGET: current=OLD8, bin/==pins/OLD8, no backup, no pins/TARGET, no
# pin-build/TARGET, out emptied, master=TARGET, a green ci record for TARGET.
reset_clean() { # $1=target full sha, $2=target s8
    rm -rf "$D/pin-build/$2" "$D/pins/$2" "$D/backups/bin-$OLD8"
    rm -f "$D/pins/current"; ln -sfn "$OLD8" "$D/pins/current"
    cp "$D/pins/$OLD8/jichi" "$D/bin/jichi"
    cp "$D/pins/$OLD8/jichi-harness" "$D/bin/jichi-harness"
    : > "$D/repin.out"
    set_master "$1"
    printf 'make ci rc=0\n' > "$CI/ci-$2.log.meta"
    printf 'ci: OK\n' > "$CI/ci-$2.log"
}
# run_repin CFGFILE SHA OUT ERR -- one `repin` invocation (the orchestrator's
# repinRun form: --config names every path, no flags). Returns rc via $? .
run_repin() {
    "$HBIN" --config "$1" repin "$2" < /dev/null > "$3" 2> "$4"
}

# --- 1 (b1): usage -- no <SHA8> -> rc 2, stderr names `usage: jichi-harness repin <SHA8>`
cfg_build "$CFG"
run_repin "$CFG" "" "$tmp/o1" "$tmp/e1"; RC1=$?
if [ "$RC1" -eq 2 ] && "$G" -qF 'usage: jichi-harness repin <SHA8>' "$tmp/e1"; then
    t_ok "1: b1 usage -- no <SHA8> -> rc 2, stderr names usage: jichi-harness repin <SHA8>"
else
    t_fail "1: b1 usage expected rc 2 + 'usage: jichi-harness repin <SHA8>' (got rc=$RC1: $(tr '\n' '|' < "$tmp/e1" | head_bytes 160))"
fi

# --- 2 (b2): sha not in push.git -> named refusal, rc 1, NO clone dir made ------
BAD8="deadbeef"
CFG_CLONE="" # ($CFG was already written by cell 1)
run_repin "$CFG" "$BAD8" "$tmp/o2" "$tmp/e2"; RC2=$?
if [ "$RC2" -eq 1 ] && "$G" -qF "repin: $BAD8 is not a commit in push.git" "$tmp/e2" \
   && [ ! -e "$D/pin-build/$BAD8" ]; then
    t_ok "2: b2 -- sha $BAD8 not in push.git -> 'repin: $BAD8 is not a commit in push.git' (rc 1), no clone dir made (order: b2 precedes b7)"
else
    t_fail "2: b2 expected rc 1 + the named refusal + no clone dir (got rc=$RC2: $(tr '\n' '|' < "$tmp/e2" | head_bytes 160); clone dir: $([ -e "$D/pin-build/$BAD8" ] && echo present || echo absent))"
fi

# --- 3 (b3a): push.git without refs/remotes/origin/master -----------------------
git clone -q "file://$BARE" "$PUSH2" 2>/dev/null
git -C "$PUSH2" update-ref -d refs/remotes/origin/master 2>/dev/null
CFG_PUSH="$PUSH2"; CFG_CLONE=""; cfg_build "$CFG"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"   # restore the defaults for the cells after
run_repin "$CFG" "$C2" "$tmp/o3" "$tmp/e3"; RC3=$?
if [ "$RC3" -eq 1 ] && "$G" -qF "repin: no origin/master in push.git" "$tmp/e3"; then
    t_ok "3: b3a -- push.git without refs/remotes/origin/master -> 'repin: no origin/master in push.git' (rc 1)"
else
    t_fail "3: b3a expected rc 1 + 'repin: no origin/master in push.git' (got rc=$RC3: $(tr '\n' '|' < "$tmp/e3" | head_bytes 160))"
fi

# --- 4 (b3): sha != fetched master (a commit BEHIND master) ---------------------
# origin/master = C2 (S8); the target C1 (OLD8) is behind it.
set_master "$C2"
CFG_PUSH="$PUSH"; CFG_CLONE=""; cfg_build "$CFG"
run_repin "$CFG" "$C1" "$tmp/o4" "$tmp/e4"; RC4=$?
if [ "$RC4" -eq 1 ] && "$G" -qF "repin: refused: $OLD8 is not GitLab master as push.git last fetched it ($S8)" "$tmp/e4"; then
    t_ok "4: b3 -- sha $OLD8 behind fetched master $S8 -> 'repin: refused: $OLD8 is not GitLab master as push.git last fetched it ($S8)' (the word GitLab stays, rc 1)"
else
    t_fail "4: b3 expected rc 1 + the not-GitLab-master refusal (got rc=$RC4: $(tr '\n' '|' < "$tmp/e4" | head_bytes 160))"
fi

# --- 5 (b4): ci meta's last line / absence / mid-file rc=0 ----------------------
reset_clean "$C2" "$S8"   # b2-b5 green before b4 (master=C2, pins/OLD8 clean)
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; cfg_build "$CFG"
# (a) last line ` rc=1`
printf 'make ci rc=0\nmake ci rc=1\n' > "$CI/ci-$S8.log.meta"
run_repin "$CFG" "$S8" "$tmp/o5a" "$tmp/e5a"; RC5a=$?
# (b) meta ABSENT
rm -f "$CI/ci-$S8.log.meta"
run_repin "$CFG" "$S8" "$tmp/o5b" "$tmp/e5b"; RC5b=$?
# (c) ` rc=0` mid-file, last line ` rc=1` (tail -1 form)
printf 'make ci rc=0\nnoise\nmake ci rc=1\n' > "$CI/ci-$S8.log.meta"
run_repin "$CFG" "$S8" "$tmp/o5c" "$tmp/e5c"; RC5c=$?
METAPATH="$CI/ci-$S8.log.meta"
if [ "$RC5a" -eq 1 ] && "$G" -qF "repin: refused: no green make ci record for $S8 ($METAPATH)" "$tmp/e5a" \
   && [ "$RC5b" -eq 1 ] && "$G" -qF "repin: refused: no green make ci record for $S8 ($METAPATH)" "$tmp/e5b" \
   && [ "$RC5c" -eq 1 ] && "$G" -qF "repin: refused: no green make ci record for $S8 ($METAPATH)" "$tmp/e5c"; then
    t_ok "5: b4 -- meta last line ' rc=1', meta ABSENT, and ' rc=0' mid-file (last ' rc=1') all -> 'repin: refused: no green make ci record for $S8 (<meta path>)' (the path named, rc 1, the tail -1 form)"
else
    t_fail "5: b4 expected the three named ci refusals (got rc=$RC5a/$RC5b/$RC5c: $(tr '\n' '|' < "$tmp/e5a" | head_bytes 120) || $(tr '\n' '|' < "$tmp/e5b" | head_bytes 120) || $(tr '\n' '|' < "$tmp/e5c" | head_bytes 120))"
fi
printf 'make ci rc=0\n' > "$CI/ci-$S8.log.meta"   # restore the green base

# --- 6 (b5a): pins/current absent / a plain file --------------------------------
reset_clean "$C2" "$S8"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; cfg_build "$CFG"
# (a) absent
rm -f "$D/pins/current"
run_repin "$CFG" "$S8" "$tmp/o6a" "$tmp/e6a"; RC6a=$?
# (b) a plain file
rm -f "$D/pins/current"; printf 'plain\n' > "$D/pins/current"
run_repin "$CFG" "$S8" "$tmp/o6b" "$tmp/e6b"; RC6b=$?
ln -sfn "$OLD8" "$D/pins/current"   # restore
if [ "$RC6a" -eq 1 ] && "$G" -qF "repin: pins/current is not a link" "$tmp/e6a" \
   && [ "$RC6b" -eq 1 ] && "$G" -qF "repin: pins/current is not a link" "$tmp/e6b"; then
    t_ok "6: b5a -- pins/current ABSENT and a plain file both -> 'repin: pins/current is not a link' (rc 1)"
else
    t_fail "6: b5a expected rc 1 + 'repin: pins/current is not a link' for absent and plain-file (got rc=$RC6a/$RC6b: $(tr '\n' '|' < "$tmp/e6a" | head_bytes 120) || $(tr '\n' '|' < "$tmp/e6b" | head_bytes 120))"
fi

# --- 7 (b5b): pins/current -> <s8> already --------------------------------------
reset_clean "$C2" "$S8"
ln -sfn "$S8" "$D/pins/current"     # current already points at the target
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o7" "$tmp/e7"; RC7=$?
ln -sfn "$OLD8" "$D/pins/current"   # restore
if [ "$RC7" -eq 1 ] && "$G" -qF "repin: refused: pins/current is $S8 already" "$tmp/e7"; then
    t_ok "7: b5b -- pins/current -> $S8 already -> 'repin: refused: pins/current is $S8 already' (rc 1, the idempotency guard)"
else
    t_fail "7: b5b expected rc 1 + 'repin: refused: pins/current is $S8 already' (got rc=$RC7: $(tr '\n' '|' < "$tmp/e7" | head_bytes 160))"
fi

# --- 8 (b7): pre-created clone dir / clone shim exit 1 / default-clone control --
# Three arms, EACH with its own single cfg_build: the old double build in arm (b)
# (CFG_CLONE=clone-fail.sh + cfg_build, then CFG_CLONE="" + cfg_build) reset the
# config BEFORE the control arm, so 8b and 8c ran identical state needing opposite
# outcomes. Arm (c) asserts the reference's clone+checkout (repin.sh:49, the
# checkout-failure refusal `the clone failed`): pin-build/$S8 exists with a
# checked-out tree at C2 and NO `the clone failed` line -- it does NOT assert the
# run's rc, because the run may refuse LATER (e.g. b5b if current was left at S8)
# or reach rc 0 (as here: after reset_clean, current=OLD8 and bin/==pins/OLD8, so
# the backup cmp passes and the repin completes green).
reset_clean "$C2" "$S8"
# (a) the per-sha clone dir pre-created (no shim)
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; cfg_build "$CFG"
mkdir -p "$D/pin-build/$S8"
run_repin "$CFG" "$S8" "$tmp/o8a" "$tmp/e8a"; RC8a=$?
rm -rf "$D/pin-build/$S8"
# (b) the repinClone shim exits 1 (its own single cfg_build)
CFG_PUSH="$PUSH"; CFG_CLONE="$SHIM/clone-fail.sh"; CFG_BUILD="$SHIM/build-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o8b" "$tmp/e8b"; RC8b=$?
rm -rf "$D/pin-build/$S8"
# (c) DEFAULT-clone control (no shim, its own single cfg_build): a real git
# clone+checkout must happen (set_master now tracks refs/heads/master, so the
# reference's plain checkout of the target's full sha resolves)
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o8c" "$tmp/e8c"; RC8c=$?
B_DIR="$D/pin-build/$S8"
CHEAD=$(git -C "$B_DIR" rev-parse HEAD 2>/dev/null)
if [ "$RC8a" -eq 1 ] && "$G" -qF "repin: refused: $B_DIR exists" "$tmp/e8a" \
   && [ "$RC8b" -eq 1 ] && "$G" -qF "repin: the clone failed" "$tmp/e8b" \
   && [ -d "$B_DIR" ] && [ "$CHEAD" = "$C2" ] && ! "$G" -qF "repin: the clone failed" "$tmp/e8c"; then
    t_ok "8: b7 -- pre-created clone dir -> 'repin: refused: <B> exists'; repinClone shim exit 1 -> 'repin: the clone failed'; the DEFAULT-clone control (no shim) ran a real clone+checkout (pin-build/$S8 present at $C2, no 'the clone failed' line; the run's later outcome is not asserted)"
else
    t_fail "8: b7 expected the three arms (got rc=$RC8a/$RC8b/$RC8c: $(tr '\n' '|' < "$tmp/e8a" | head_bytes 120) || $(tr '\n' '|' < "$tmp/e8b" | head_bytes 120); control B: $([ -d "$B_DIR" ] && echo "present HEAD=$CHEAD" || echo absent))"
fi
rm -rf "$B_DIR"

# --- 9 (b8): the build stand-in exits 1 -----------------------------------------
reset_clean "$C2" "$S8"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-fail.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o9" "$tmp/e9"; RC9=$?
BLOG="$D/pin-build/$S8.build.log"
if [ "$RC9" -eq 1 ] && "$G" -qF "repin: the build failed: $BLOG" "$tmp/e9" \
   && [ -f "$BLOG" ] && "$G" -qF 'build-broken-shim' "$BLOG"; then
    t_ok "9: b8 -- build stand-in exit 1 -> 'repin: the build failed: $BLOG' (the path named, rc 1, the log exists with the stand-in's stderr)"
else
    t_fail "9: b8 expected rc 1 + the named build-failed refusal + the log (got rc=$RC9: $(tr '\n' '|' < "$tmp/e9" | head_bytes 160); log: $([ -f "$BLOG" ] && head_bytes 80 "$BLOG" || echo absent))"
fi
rm -rf "$D/pin-build/$S8" "$BLOG"

# --- 10 (b9): the build leaves a porcelain file --------------------------------
reset_clean "$C2" "$S8"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-porcelain.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o10" "$tmp/e10"; RC10=$?
if [ "$RC10" -eq 1 ] && "$G" -qF "repin: the build left 1 porcelain line(s) in $D/pin-build/$S8" "$tmp/e10"; then
    t_ok "10: b9 -- build stand-in leaves an untracked file -> 'repin: the build left 1 porcelain line(s) in <clone>' (rc 1)"
else
    t_fail "10: b9 expected rc 1 + the named porcelain refusal (got rc=$RC10: $(tr '\n' '|' < "$tmp/e10" | head_bytes 160))"
fi
rm -rf "$D/pin-build/$S8" "$D/pin-build/$S8.build.log"

# --- 11 (b10): the --version stamps (wrong / missing / dirty) ------------------
reset_clean "$C2" "$S8"
# (a) jichi's stamp says build: other123 (the loop order is jichi THEN harness)
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-badstamp.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o11a" "$tmp/e11a"; RC11a=$?
rm -rf "$D/pin-build/$S8" "$D/pin-build/$S8.build.log"
# (b) jichi-harness's stamp is missing (jichi is clean, the harness file absent)
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-badstamp2.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o11b" "$tmp/e11b"; RC11b=$?
rm -rf "$D/pin-build/$S8" "$D/pin-build/$S8.build.log"
# (c) a `(dirty)` suffix: the grep -qx whole-line form must refuse
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-dirty.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o11c" "$tmp/e11c"; RC11c=$?
rm -rf "$D/pin-build/$S8" "$D/pin-build/$S8.build.log"
if [ "$RC11a" -eq 1 ] && "$G" -qF "repin: jichi --version does not say build: $S8" "$tmp/e11a" \
   && [ "$RC11b" -eq 1 ] && "$G" -qF "repin: jichi-harness --version does not say build: $S8" "$tmp/e11b" \
   && [ "$RC11c" -eq 1 ] && "$G" -qF "repin: jichi --version does not say build: $S8" "$tmp/e11c"; then
    t_ok "11: b10 -- jichi stamp 'build: other123' -> 'repin: jichi --version does not say build: <s8>'; a missing jichi-harness stamp -> its named refusal; a '(dirty)' suffix -> refused (the whole-line form, all rc 1)"
else
    t_fail "11: b10 expected the three stamp refusals (got rc=$RC11a/$RC11b/$RC11c: $(tr '\n' '|' < "$tmp/e11a" | head_bytes 110) || $(tr '\n' '|' < "$tmp/e11b" | head_bytes 110) || $(tr '\n' '|' < "$tmp/e11c" | head_bytes 110))"
fi

# --- 12 (b11): pins/<s8> pre-created -------------------------------------------
reset_clean "$C2" "$S8"
mkdir -p "$D/pins/$S8"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o12" "$tmp/e12"; RC12=$?
if [ "$RC12" -eq 1 ] && "$G" -qF "repin: refused: pins/$S8 exists" "$tmp/e12"; then
    t_ok "12: b11 -- pins/$S8 pre-created -> 'repin: refused: pins/$S8 exists' (rc 1, a pin dir is immutable once made)"
else
    t_fail "12: b11 expected rc 1 + 'repin: refused: pins/$S8 exists' (got rc=$RC12: $(tr '\n' '|' < "$tmp/e12" | head_bytes 160))"
fi
rm -rf "$D/pins/$S8"

# --- 13 (b12): the backup is taken once, from the running bin/; the cmp guard --
# The reference's order is load-bearing (repin.sh): clone+build (:49-50), the
# b9 dirty-build refusal (:51-52), the b10 stamps (:53-55), the b11 pins/$s
# exists check (:56) and the pin mkdir+cp (:57) -- the pin is CREATED BEFORE
# the backup -- THEN the backup (:59-60: mkdir -p $bk && cp the running bin/*
# into it ONLY when $bk is absent), the b12 cmp (:61-63) and the swap
# (:64-68). So a run refused at the cmp leaves pins/$s PRESENT. b9 runs
# BEFORE the backup, so a clean build that is refused later still makes a
# backup (only b9/b11 refusals precede it). The cmp is BACKUP vs PINS/$old --
# NOT backup vs the running bin/: with a clean running pair (bin/ ==
# pins/OLD8) the first snapshot EQUALS pins/OLD8 and the cmp PASSES. The only
# way a first run reaches a FAILED cmp is to corrupt the running bin/ BEFORE
# the snapshot, so run A does exactly that: tamper bin/ first, then repin S8
# -- the backup bin-OLD8 is taken from the corrupted bin/ (the first
# snapshot, provenance the cell asserts), the cmp of that tampered backup
# against pins/OLD8/jichi fails -> the named refusal, NO swap (pins/$S8 is
# PRESENT -- the reference made it at :57 before the backup -- but current is
# still OLD8 and bin/ still the tampered bytes: nothing swapped). Run B then
# proves the not-rewritten arm: the backup EXISTS from run A, the driver
# cleans its own state (pin-build/$S8 + its log + pins/$S8 -- the VERB never
# rms), master moves to C3, the running bin/ is tampered with DIFFERENT
# bytes, and repin S9 (a DIFFERENT target: pin-build/$S9 is fresh at b7; the
# backup still keys to bin-OLD8 since current is still OLD8) -> the backup is
# NOT rewritten (still the run-A bytes), the cmp still fails -> the SAME
# named refusal, NO swap.
reset_clean "$C2" "$S8"   # current=OLD8, bin/==pins/OLD8, no backup, master=C2, green ci for S8
BK_A="$D/backups/bin-$OLD8"
# Run A: TAMPER the running bin/ FIRST, then repin S8 (build-ok, clean).
printf 'tampered-running-jichi\n' > "$D/bin/jichi"
TAMPER_A=$(cat "$D/bin/jichi")
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o13a" "$tmp/e13a"; RCA=$?
# Capture run A's state NOW, before the inter-run cleanup: the reference makes
# pins/$S8 (repin.sh:57) BEFORE the backup it is refused at, so the run leaves
# it present; and bin/ still holds the run-A tampered bytes.
PINA_PRESENT=0; [ -e "$D/pins/$S8" ] && PINA_PRESENT=1
BINA=$(cat "$D/bin/jichi")
# Inter-run cleanup (driver state, the verb never rms): drop the pin-build
# clone + its log and the pin run A CREATED (repin.sh:57, before the backup),
# KEEP the backup -- that is the point of the not-rewritten arm.
rm -rf "$D/pin-build/$S8" "$D/pin-build/$S8.build.log" "$D/pins/$S8"
# Run B: WITHOUT removing bin-$OLD8, point master at C3, tamper the running
# bin/ with DIFFERENT bytes and repin S9 (current is still OLD8 after run A's
# stop, so the backup still keys to bin-$OLD8).
set_master "$C3"
printf 'tampered-running-v2\n' > "$D/bin/jichi"
TAMPER_B=$(cat "$D/bin/jichi")
run_repin "$CFG" "$S9" "$tmp/o13b" "$tmp/e13b"; RCB=$?
if [ "$RCA" -eq 1 ] && "$G" -qF "repin: the backup $BK_A/jichi differs from pins/$OLD8/jichi: stop before the swap" "$tmp/e13a" \
   && [ -d "$BK_A" ] && "$G" -qF 'tampered-running-jichi' "$BK_A/jichi" \
   && [ "$PINA_PRESENT" -eq 1 ] \
   && [ "$(readlink "$D/pins/current" 2>/dev/null)" = "$OLD8" ] \
   && [ "$BINA" = "$TAMPER_A" ] \
   && [ "$RCB" -eq 1 ] && "$G" -qF "repin: the backup $BK_A/jichi differs from pins/$OLD8/jichi: stop before the swap" "$tmp/e13b" \
   && [ -d "$BK_A" ] && "$G" -qF 'tampered-running-jichi' "$BK_A/jichi" \
   && ! "$G" -qF 'tampered-running-v2' "$BK_A/jichi" 2>/dev/null \
   && [ "$(readlink "$D/pins/current" 2>/dev/null)" = "$OLD8" ] \
   && [ "$(cat "$D/bin/jichi")" = "$TAMPER_B" ]; then
    t_ok "13: b12 -- run A: the running bin/ TAMPERED first, repin S8 -> the backup $BK_A is taken from the corrupted bin/ (repin.sh:59-60) and the b12 cmp of BACKUP vs PINS/$OLD8 (repin.sh:61-63, NOT vs the running bin/) fails -> 'repin: the backup $BK_A/jichi differs from pins/$OLD8/jichi: stop before the swap' (rc 1); the reference creates pins/$S8 BEFORE the backup (repin.sh:57 vs :59-63) so pins/$S8 is PRESENT after the refusal, yet NO swap (current still $OLD8, bin/ still the tampered bytes); the backup EXISTS holding 'tampered-running-jichi' (provenance: the snapshot came from the corrupted bin/). Run B: master C3, the running bin/ tampered with DIFFERENT bytes, repin S9 (pin-build/$S9 fresh; the backup still keys to bin-$OLD8) -> the backup is NOT rewritten (still the run-A bytes, no 'tampered-running-v2'), the SAME named refusal (rc 1), NO swap (current still $OLD8, bin/ still the run-B tampered bytes)"
else
    t_fail "13: b12 expected run A rc 1 + the backup-cmp refusal + the backup holding the tampered first-snapshot bytes + pins/$S8 PRESENT (the reference makes it at :57, before the backup) + no swap, and run B rc 1 + the SAME refusal + the backup NOT rewritten (no v2 bytes) + no swap (got rcA=$RCA: $(tr '\n' '|' < "$tmp/e13a" | head_bytes 160); rcB=$RCB: $(tr '\n' '|' < "$tmp/e13b" | head_bytes 160); backup jichi: $(head_bytes 40 "$BK_A/jichi" 2>/dev/null | tr '\n' '|'); pins/$S8 after run A: $([ "$PINA_PRESENT" -eq 1 ] && echo present || echo absent); current: $(readlink "$D/pins/current" 2>/dev/null); bin: $(head_bytes 24 "$D/bin/jichi" 2>/dev/null | tr '\n' '|'))"
fi
# Cleanup: remove the backup the run made (KEEP-then-restore is the cell's
# own driver state), the pin-build clones + logs, pins/$S8, restore the clean
# running pair, current=OLD8, master=C2 -- the cells after 13 are unaffected.
rm -rf "$D/backups/bin-$OLD8" "$D/pin-build/$S8" "$D/pin-build/$S8.build.log" \
       "$D/pin-build/$S9" "$D/pin-build/$S9.build.log" "$D/pins/$S8" "$D/pins/$S9"
cp "$D/pins/$OLD8/jichi" "$D/bin/jichi"; cp "$D/pins/$OLD8/jichi-harness" "$D/bin/jichi-harness"
ln -sfn "$OLD8" "$D/pins/current"; set_master "$C2"

# --- 14 (b13/b14/b15 + b16/b23): the SUCCESS with the REAL doctors -------------
# Two repins: OLD8 -> S8 (master C2), then S8 -> S9 (master C3). The live-doctor
# stand-in is the measured-green mockmodel recipe for BOTH models; the
# harness-doctor stand-in is the port's own offline doctor. The backup's
# PROVENANCE is asserted positively by cmp (the port's b12 snapshot is a copy of
# the running bin/ at swap time): after repin 1 the backup bin-$OLD8/jichi
# cmp-equals pins/$OLD8/jichi (the running pair was clean), after repin 2 the
# backup bin-$S8/jichi cmp-equals pins/$S8/jichi (the running bin/ was the
# just-swapped bin/ == pins/$S8).
reset_clean "$C2" "$S8"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; CFG_DOC="$SHIM/hdoctor-ok.sh"; CFG_LIVE="$SHIM/live-real.sh"; cfg_build "$CFG"
BK1="$D/backups/bin-$OLD8"
run_repin "$CFG" "$S8" "$tmp/o14a" "$tmp/e14a"; RC14a=$?
OUT1=0; [ -f "$D/repin.out" ] && OUT1=$(wc -c < "$D/repin.out")
# Repin 2: current is now S8 (a real pin after run 1), so OLD = S8.
BK2="$D/backups/bin-$S8"
if [ "$RC14a" -eq 0 ]; then
    set_master "$C3"
    CFG_LIVE="$SHIM/live-real.sh"
    CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; CFG_DOC="$SHIM/hdoctor-ok.sh"; CFG_LIVE="$SHIM/live-real.sh"; cfg_build "$CFG"
    run_repin "$CFG" "$S9" "$tmp/o14b" "$tmp/e14b"; RC14b=$?
else
    RC14b=2
fi
OUT2=0; [ -f "$D/repin.out" ] && OUT2=$(wc -c < "$D/repin.out")
CUR_NOW=$(readlink "$D/pins/current" 2>/dev/null)
# F7 (wave-2c): b6's summary is said BEFORE the build (repin.sh:44), so it is
# the out file's FIRST line. reset_clean writes a one-line meta ('make ci
# rc=0\n'), so the full line is known: the reference's `tr '\n' ' ' < "$meta"`
# turns the trailing newline into a TRAILING SPACE (the $(...) strips newlines,
# not that space), so the line ends `...make ci rc=0 )` (space before the close
# paren). The CONTROL is the negative: the first line must not be the b15
# swap/cmp/backup line or any other say line -- that is what makes the
# b6-first tooth real (it fails if the port ever says b6 after b9/b10).
B6LINE=$(sed -n 1p "$tmp/o14a" 2>/dev/null)
B6WANT="repin: $OLD8 -> $S8 (master; make ci: make ci rc=0 )"
B6NEG=1
printf '%s\n' "$B6LINE" | "$G" -qE 'bin/ == pins/|backup|src/harness files changed|doctor|done ' && B6NEG=0
LASTLINE=""; [ -f "$tmp/o14b" ] && LASTLINE=$(tail -1 "$tmp/o14b")
if [ "$RC14a" -eq 0 ] && [ "$RC14b" -eq 0 ] \
   && [ "$B6LINE" = "$B6WANT" ] && [ "$B6NEG" -eq 1 ] \
   && cmp -s "$D/bin/jichi" "$D/pins/$S9/jichi" && cmp -s "$D/bin/jichi-harness" "$D/pins/$S9/jichi-harness" \
   && [ -L "$D/pins/current" ] && [ "$CUR_NOW" = "$S9" ] \
   && [ -d "$BK1" ] && cmp -s "$BK1/jichi" "$D/pins/$OLD8/jichi" \
   && [ -d "$BK2" ] && cmp -s "$BK2/jichi" "$D/pins/$S8/jichi" \
   && "$G" -qF "repin: $OLD8 -> $S8 (master; make ci: " "$tmp/o14a" \
   && "$G" -qF "repin: bin/ == pins/$S9 (cmp); backup " "$tmp/o14b" \
   && "$G" -qF "repin: done $S9, doctors clean" "$tmp/o14b" \
   && [ -d "$D/pin-build/$S8" ] && [ -d "$D/pin-build/$S9" ] \
   && [ -d "$D/pins/$OLD8" ] && [ -d "$D/pins/$S8" ] && [ -d "$D/pins/$S9" ] \
   && [ -d "$D/backups/bin-$OLD8" ] && [ -d "$D/backups/bin-$S8" ] \
   && [ -f "$D/repin.out" ] && [ "$OUT2" -gt "$OUT1" ] \
   && "$G" -qF "repin: $OLD8 -> $S8 (master; make ci: " "$D/repin.out" \
   && "$G" -qF "repin: done $S9, doctors clean" "$D/repin.out" \
   && "$G" -qE '^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}' "$D/repin.out"; then
    t_ok "14: b13/b14/b15 SUCCESS (both repins rc 0) -- bin/ == pins/$S9 (cmp both); pins/current is a LINK reading $S9; backup provenance by cmp: bin-$OLD8/jichi == pins/$OLD8/jichi (repin 1's snapshot came from the clean running pair) and bin-$S8/jichi == pins/$S8/jichi (repin 2's snapshot came from the just-swapped bin/ == pins/$S8); b6-first proven by the out file's FIRST line being exactly 'repin: $OLD8 -> $S8 (master; make ci: make ci rc=0)' + the negative control (it is NO other say line: no build/cmp/backup/doctor/done) + b15 swap line + b22 last line 'repin: done $S9, doctors clean' present; b16 nothing deleted (clone dirs, both pins dirs, backups, the out file exist); b23 the out file carries STAMPED lines and GROWs over the two repins (never truncates) -- with the REAL offline harness doctor and the REAL mockmodel jichi doctor --live"
else
    t_fail "14: SUCCESS expected rc 0 + the swap/pin/backup/out assertions (got rcA=$RC14a rcB=$RC14b; current=$CUR_NOW; BK1 jichi==pins/$OLD8: $(cmp -s "$BK1/jichi" "$D/pins/$OLD8/jichi" 2>/dev/null && echo yes || echo no); BK2 jichi: $(head_bytes 32 "$BK2/jichi" 2>/dev/null | tr '\n' '|'); out1=$OUT1 out2=$OUT2; b6 first line: $(head_bytes 120 "$tmp/o14a" | tr '\n' '|') (want: $B6WANT; other-say control: $([ "$B6NEG" -eq 1 ] && echo passed || echo FAILED)); last: $([ -f "$tmp/o14b" ] && tr '\n' '|' < "$tmp/o14b" | head_bytes 120))"
fi
# Leave the fixture at S9 for the b17/b19/b20/b21/b22 cells (re-pin from S9
# is not possible: current=S9; those cells re-pin S8->S9 or S9 is the target,
# so restore a clean OLD8 start before each).
reset_clean "$C2" "$S8"

# --- 15 (b17): the src/harness changed-count, both arms -------------------------
# Run A (start made explicit: reset_clean -> current=OLD8, bin/==pins/OLD8,
# master=C2, a green ci record for S8, no pin-build/$S8): repin OLD8 -> S8;
# C1..C2 differs in 2 src/harness files -> `: 2`.
reset_clean "$C2" "$S8"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o15a" "$tmp/e15a"; RC15a=$?
# Run B: the driver cleans its OWN inter-cell state (b16 binds the VERB, not the
# fixture): cell 14's repin 2 left pin-build/$S9 + pins/$S9, which would refuse
# here at b7/b11. Remove them (keep what the cell needs: current=S8,
# bin/==pins/$S8 from run A's swap, master, the green ci for S9). Then
# set_master C3 and repin S8 -> S9; C2..C3 differs in 0 src/harness files -> `: 0`.
rm -rf "$D/pin-build/$S9" "$D/pin-build/$S9.build.log" "$D/pins/$S9"
set_master "$C3"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S9" "$tmp/o15b" "$tmp/e15b"; RC15b=$?
if [ "$RC15a" -eq 0 ] && "$G" -qF "repin: src/harness files changed $OLD8..$S8: 2 (the loop process keeps $OLD8's code until it restarts)" "$tmp/o15a" \
   && [ "$RC15b" -eq 0 ] && "$G" -qF "repin: src/harness files changed $S8..$S9: 0 (the loop process keeps $S8's code until it restarts)" "$tmp/o15b"; then
    t_ok "15: b17 -- OLD8..S8 (2 src/harness files changed) -> the line names ': 2'; S8..S9 (0 diff) -> ': 0' (the loop-restart measurement, both ways, rc 0)"
else
    t_fail "15: b17 expected ': 2' and ': 0' (got rcA=$RC15a rcB=$RC15b: $("$G" 'src/harness files changed' "$tmp/o15a" 2>/dev/null | head -1 | head_bytes 120) || $("$G" 'src/harness files changed' "$tmp/o15b" 2>/dev/null | head -1 | head_bytes 120))"
fi
reset_clean "$C2" "$S8"

# --- 16 (b19): a NOT-clean harness doctor -- the swap had already happened ------
reset_clean "$C2" "$S8"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; CFG_DOC="$SHIM/hdoctor-notclean.sh"; CFG_LIVE="$SHIM/live-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o16" "$tmp/e16"; RC16=$?
if [ "$RC16" -eq 1 ] && "$G" -qF "repin: done $S8, doctors NOT clean" "$tmp/o16" \
   && [ -d "$D/pins/$S8" ] && [ "$(readlink "$D/pins/current" 2>/dev/null)" = "$S8" ] \
   && cmp -s "$D/bin/jichi" "$D/pins/$S8/jichi"; then
    t_ok "16: b19 -- the harness-doctor stand-in's '1 ok, 0 warnings, 1 problems' -> rc 1 + 'repin: done $S8, doctors NOT clean', and the swap HAD happened (pins/$S8 present, pins/current -> $S8, bin/ == pins/$S8: the doctors run AFTER the swap, a not-clean doctor does not un-swap)"
else
    t_fail "16: b19 expected rc 1 + the NOT-clean last line + the completed swap (got rc=$RC16: $(tr '\n' '|' < "$tmp/o16" | head_bytes 160); current: $(readlink "$D/pins/current" 2>/dev/null); pins/$S8: $([ -d "$D/pins/$S8" ] && echo present || echo absent))"
fi
reset_clean "$C2" "$S8"

# --- 17 (b20): the key never reaches the live doctor's argv; it is in the env ---
# Cell 16 set CFG_DOC=hdoctor-notclean.sh and never reset it; this cell's state
# needs the harness-doctor stand-in CLEAN (rc 0, the comment's contract), so the
# config line resets CFG_DOC back to hdoctor-ok.sh. CFG_LIVE is this cell's own
# argv stand-in (set below, explicitly).
reset_clean "$C2" "$S8"
rm -f "$SHIM/argv-dump.txt"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; CFG_DOC="$SHIM/hdoctor-ok.sh"; CFG_LIVE="$SHIM/live-argv.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o17" "$tmp/e17"; RC17=$?
if [ "$RC17" -eq 0 ] && [ -f "$SHIM/argv-dump.txt" ] \
   && ! "$G" -qF "$CANARY" "$SHIM/argv-dump.txt" \
   && "$G" -qF "JICHI_API_KEY length=$CANLEN" "$tmp/o17"; then
    t_ok "17: b20 -- the live-doctor stand-in's argv dump holds NO canary (the key is never on argv) and its env probe printed 'JICHI_API_KEY length=$CANLEN' (the env delivery worked; rc 0)"
else
    t_fail "17: b20 expected rc 0 + a canary-free argv dump + the env length (got rc=$RC17; dump: $(head_bytes 200 "$SHIM/argv-dump.txt" 2>/dev/null | tr '\n' '|'); out: $(tr '\n' '|' < "$tmp/o17" | head_bytes 160))"
fi
reset_clean "$C2" "$S8"

# --- 18 (b21): output holding the key's 12-char prefix is WITHHELD (per model) --
reset_clean "$C2" "$S8"
rm -f "$D/pin-build/$S8.doctor-live-M1.txt" "$D/pin-build/$S8.doctor-live-M2.txt"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; CFG_LIVE="$SHIM/live-canary.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o18" "$tmp/e18"; RC18=$?
# The census (the config_credentials.sh form) scans the VERB's output surfaces:
# repin.out, the per-sha clone (the build log + the doctor files), the pins,
# the running bin/, the backups, and this cell's captured stdout/stderr. The
# driver's own canary source ($KEY) and the stand-ins' jichi configs ($D/conf,
# the live doctor's apiKey home) are legitimately out of scope: the guard is
# that the key never reaches what the verb PRINTS or PINS. The teeth unit b21
# guards the key's 12-char PREFIX (cut -c1-12), not the full canary -- and the
# planted leak (the live-canary.sh stand-in) holds ONLY $CANPFX, which is NOT a
# substring of the full canary, so a PREFIX-ONLY leak on the verb's output
# surfaces must be censused too. The prefix subsumes the full canary (it is a
# substring of it), so the prefix scan is the stronger one; both are run and
# both are required empty, for clarity.
LEAKS=$(find "$D/pin-build" "$D/backups" "$D/bin" "$D/pins" "$D/repin.out" "$tmp/o18" "$tmp/e18" -type f 2>/dev/null | while read -r f; do
    "$G" -qF "$CANARY" "$f" 2>/dev/null && printf '%s ' "$f"
done)
PFXLEAKS=$(find "$D/pin-build" "$D/backups" "$D/bin" "$D/pins" "$D/repin.out" "$tmp/o18" "$tmp/e18" -type f 2>/dev/null | while read -r f; do
    "$G" -qF "$CANPFX" "$f" 2>/dev/null && printf '%s ' "$f"
done)
if [ "$RC18" -eq 1 ] && "$G" -qF "repin: doctor --live M1: output WITHHELD (it holds the key's prefix)" "$tmp/o18" \
   && [ ! -e "$D/pin-build/$S8.doctor-live-M1.txt" ] \
   && [ -e "$D/pin-build/$S8.doctor-live-M2.txt" ] \
   && [ -z "$LEAKS" ] && [ -z "$PFXLEAKS" ]; then
    t_ok "18: b21 -- the live-doctor stand-in's M1 output holds the canary's 12-char prefix -> 'repin: doctor --live M1: output WITHHELD (it holds the key's prefix)' (rc 1, the doctor-live-M1 file NOT written, the SECOND model M2 still ran and its file exists: the continue, not the abort), and NEITHER the canary NOR its 12-char prefix is in NO file under the verb's output surfaces (find + the driver's guarded grep, both the full-canary and the prefix-only scans empty)"
else
    t_fail "18: b21 expected rc 1 + the WITHHELD line + M2's file + no canary or prefix leak (got rc=$RC18: $(tr '\n' '|' < "$tmp/o18" | head_bytes 160); M1 file: $([ -e "$D/pin-build/$S8.doctor-live-M1.txt" ] && echo WRITTEN || echo absent); M2 file: $([ -e "$D/pin-build/$S8.doctor-live-M2.txt" ] && echo present || echo absent); canary leaks: $LEAKS; prefix leaks: $PFXLEAKS)"
fi
reset_clean "$C2" "$S8"

# --- 19 (b22): the exit census over the recorded rcs -----------------------------
# One re-run per recorded class: a policy refusal (rc 1), the usage (rc 2), a
# success + clean doctors (rc 0), a success + not-clean (rc 1).
reset_clean "$C2" "$S8"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "" "$tmp/o19u" "$tmp/e19u"; RC19U=$?
rm -f "$D/pins/current"; run_repin "$CFG" "$S8" "$tmp/o19r" "$tmp/e19r"; RC19R=$?
reset_clean "$C2" "$S8"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; CFG_DOC="$SHIM/hdoctor-ok.sh"; CFG_LIVE="$SHIM/live-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o19c" "$tmp/e19c"; RC19C=$?
reset_clean "$C2" "$S8"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; CFG_DOC="$SHIM/hdoctor-notclean.sh"; CFG_LIVE="$SHIM/live-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o19nc" "$tmp/e19nc"; RC19NC=$?
if [ "$RC19U" -eq 2 ] && [ "$RC19R" -eq 1 ] && [ "$RC19C" -eq 0 ] && [ "$RC19NC" -eq 1 ]; then
    t_ok "19: b22 census -- usage rc 2, a policy refusal rc 1, success + clean doctors rc 0, success + not-clean rc 1 (the exit contract holds)"
else
    t_fail "19: b22 census mismatch (got usage=$RC19U refusal=$RC19R clean=$RC19C notclean=$RC19NC)"
fi

# --- 20 (i-1, b10's ANY-line form): the build line is NOT the last line -------
# M1203-i's finding i-1: the port compares the LAST line of `--version`
# (rr_tail1), the reference (repin.sh:54) greps ANY whole line. Since M1133
# the real `jichi --version` prints `build: <s8>` and THEN a `libcurl: ...`
# line, so every stand-in in cells 1-19 (build line FINAL) is the special
# case; this cell is the neighbour. Arm (a) the real shape COMPLETES (rc 0,
# the doctors are the default stand-ins -- hdoctor-ok.sh + live-ok.sh, both
# clean); arm (b) the same shape with `(dirty)` on the middle line is STILL
# refused whole-line. Two arms, each with its OWN single cfg_build (the
# per-arm idiom of cells 8/11). Inter-arm cleanup is driver state (the verb
# never rms, b16).
reset_clean "$C2" "$S8"
# (a) the real measured shape: build line MIDDLE (jichi), build line LAST
# (jichi-harness) -> the ANY-whole-line form accepts both legs -> rc 0.
# The harness-doctor stand-in is reset to CLEAN here (the cell's contract):
# cell 19's last cfg_build left CFG_DOC at hdoctor-notclean.sh, and an rc 0
# needs a clean doctor (the cell-14/17 reset idiom).
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-realshape.sh"; CFG_DOC="$SHIM/hdoctor-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o20a" "$tmp/e20a"; RC20a=$?
# Capture the success state NOW, before the inter-arm cleanup deletes it:
# the swap happened (bin/ == pins/$S8), current moved, the last line is b22's.
CUR20a=$(readlink "$D/pins/current" 2>/dev/null)
SWAP20a=0; cmp -s "$D/bin/jichi" "$D/pins/$S8/jichi" 2>/dev/null && SWAP20a=1
# Inter-arm cleanup (driver state, the verb never rms): the run's own clone,
# its log and the pin it created, and pins/current back to $OLD8 -- arm (a)
# MOVED it to $S8 (the success swap), and arm (b) must see $OLD8, or b5b
# refuses `pins/current is $S8 already` before the b10 stamp arm it tests.
rm -rf "$D/pin-build/$S8" "$D/pin-build/$S8.build.log" "$D/pins/$S8"
ln -sfn "$OLD8" "$D/pins/current"
# (b) the real shape with `build: <s8> (dirty)` on the middle line -> the
# WHOLE-line form refuses (not last-line, not substring): rc 1, byte-exact.
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-realdirty.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o20b" "$tmp/e20b"; RC20b=$?
rm -rf "$D/pin-build/$S8" "$D/pin-build/$S8.build.log" "$D/pins/$S8"
if [ "$RC20a" -eq 0 ] && [ "$SWAP20a" -eq 1 ] && [ "$CUR20a" = "$S8" ] \
   && "$G" -qF "repin: done $S8, doctors clean" "$tmp/o20a" \
   && [ "$RC20b" -eq 1 ] && "$G" -qF "repin: jichi --version does not say build: $S8" "$tmp/e20b"; then
    t_ok "20: i-1 (b10's ANY-line form) -- arm (a) the REAL shape (jichi's build line MIDDLE, then a libcurl line last; harness's build line last) COMPLETES rc 0 (pins/current -> $S8, the `done $S8, doctors clean` last line: the port must accept ANY whole line, the reference's grep -qx form, repin.sh:54); arm (b) the same shape with 'build: <s8> (dirty)' on the middle line is STILL refused rc 1 with the byte-exact 'repin: jichi --version does not say build: $S8' (whole-line form kept: a suffix on a middle line is not a clean whole line)"
else
    t_fail "20: i-1 expected arm (a) rc 0 + success state (got rc=$RC20a; current=$CUR20a; last: $([ -f "$tmp/o20a" ] && tr '\n' '|' < "$tmp/o20a" | head_bytes 120); err: $(tr '\n' '|' < "$tmp/e20a" | head_bytes 120)) and arm (b) rc 1 + the byte-exact whole-line refusal (got rc=$RC20b: $(tr '\n' '|' < "$tmp/e20b" | head_bytes 160))"
fi
reset_clean "$C2" "$S8"

# --- 21 (i-2, the defaults): the six <host.dir>/... paths resolve to OWN storage
# M1203-i's finding i-2: the port writes every absent-field default into ONE
# shared buffer (char def[RR_CAP_PATH]) and points all six locals at it, so
# with two or more defaults absent -- the REFERENCE's own shape (repin.sh
# hard-codes $H/push.git, $H/pin-build, pins/, $D/bin, $D/backups,
# $H/repin.out from its own two dirs) -- every local reads the LAST value
# written. The cfg_build above names all six paths explicitly in every cell,
# so cells 1-19 cannot see it; this cell does. Arm (a): a config naming ONLY
# host.dir/pushRepo/ciLogDir + repinModels + the shimmable commands (NO
# repinBuildDir/pinsDir/binDir/backupDir/repinOut) completes rc 0, and the
# run's own artefacts prove each default resolved to its OWN path. Arm (b):
# everything named EXCEPT repinOut (one single-defaulted field among named
# ones: the single-defaulted field resolves to its OWN default path among
# the named ones). Each arm has its own single heredoc (the per-arm
# idiom); inter-arm cleanup is driver state.
reset_clean "$C2" "$S8"
# (a) the reference's shape: only host.dir/pushRepo/ciLogDir named (plus the
# loader-required dir/modelKeyFile/jichiConfig and the shimmable commands).
cat > "$CFG" <<EOF
{"dir":"$D","modelKeyFile":"$KEY","gitTokenFile":"$KEY","jichiConfig":"$D/conf/jichi-config.json",
 "host":{"dir":"$D","pushRepo":"$PUSH","ciLogDir":"$CI",
  "repinModels":"M1 M2","repinBuildRun":"$SHIM/build-ok.sh",
  "repinDoctor":"$SHIM/hdoctor-ok.sh","repinLiveDoctor":"$SHIM/live-ok.sh <m>"}}
EOF
run_repin "$CFG" "$S8" "$tmp/o21a" "$tmp/e21a"; RC21a=$?
OUT21a=0; [ -f "$D/repin.out" ] && OUT21a=$(wc -c < "$D/repin.out")
CUR21a=$(readlink "$D/pins/current" 2>/dev/null)
PBDIR_OK=0; [ -d "$D/pin-build/$S8" ] && PBDIR_OK=1
SWAP_OK=0; cmp -s "$D/bin/jichi" "$D/pins/$S8/jichi" 2>/dev/null && SWAP_OK=1
BK_OK=0; [ -d "$D/backups/bin-$OLD8" ] && BK_OK=1
OUTFILE_OK=0; [ -f "$D/repin.out" ] && OUTFILE_OK=1
# Inter-arm cleanup (driver state, the verb never rms): the run's clone, log,
# pin, backup; the out FILE the defaults resolved to is emptied, not removed.
rm -rf "$D/pin-build/$S8" "$D/pin-build/$S8.build.log" "$D/pins/$S8" "$D/backups/bin-$OLD8"
: > "$D/repin.out"
reset_clean "$C2" "$S8"
# (b) one single-defaulted field among named ones: everything named EXCEPT
# repinOut (the note's "proves the non-aliased resolution there too": the
# single-defaulted field must land on its OWN <host.dir>/repin.out default,
# not on one of the named ones). The key is absent, not empty, so the
# note's arm ("a config naming everything EXCEPT repinOut") is exact. Its
# own single heredoc (cfg_build always names the six paths, which is the
# whole point of NOT using it here).
cat > "$CFG" <<EOF
{"dir":"$D","modelKeyFile":"$KEY","gitTokenFile":"$KEY","jichiConfig":"$D/conf/jichi-config.json",
 "host":{"dir":"$D","pushRepo":"$PUSH","ciLogDir":"$CI",
  "repinBuildDir":"$D/pin-build","pinsDir":"$D/pins","binDir":"$D/bin","backupDir":"$D/backups",
  "repinModels":"M1 M2","repinBuildRun":"$SHIM/build-ok.sh",
  "repinDoctor":"$SHIM/hdoctor-ok.sh","repinLiveDoctor":"$SHIM/live-ok.sh <m>"}}
EOF
: > "$D/repin.out"
OUT21b0=0; [ -f "$D/repin.out" ] && OUT21b0=$(wc -c < "$D/repin.out")
run_repin "$CFG" "$S8" "$tmp/o21b" "$tmp/e21b"; RC21b=$?
OUT21b=0; [ -f "$D/repin.out" ] && OUT21b=$(wc -c < "$D/repin.out")
if [ "$RC21a" -eq 0 ] && [ "$CUR21a" = "$S8" ] && [ "$PBDIR_OK" -eq 1 ] \
   && [ "$SWAP_OK" -eq 1 ] && [ "$BK_OK" -eq 1 ] && [ "$OUTFILE_OK" -eq 1 ] \
   && [ "$OUT21a" -gt 0 ] \
   && [ "$RC21b" -eq 0 ] && [ "$OUT21b" -gt "$OUT21b0" ]; then
    t_ok "21: i-2 (the defaults) -- arm (a) a config naming ONLY host.dir/pushRepo/ciLogDir + repinModels + the shimmable commands (the REFERENCE's shape: every path from its <host.dir>/... default) completes rc 0, and the run's own artefacts prove each default resolved to its OWN path: pin-build/$S8/ is a DIR (not under a repin.out file), bin/jichi == pins/$S8/jichi (the swap happened at the defaulted bin/pins), backups/bin-$OLD8/ exists (the snapshot at the defaulted backup), repin.out is a FILE that grew (the defaulted out, a file not a dir); arm (b) everything named EXCEPT repinOut (one single-defaulted field among named ones) -> rc 0 + the default $D/repin.out FILE grew (the single-defaulted field resolves to its OWN default path among the named ones)"
else
    t_fail "21: i-2 expected both arms rc 0 with the artefacts proving each default is its OWN path (got arm a: rc=$RC21a current=$CUR21a pin-build-dir=$PBDIR_OK swap-cmp=$SWAP_OK backup-dir=$BK_OK out-file=$OUTFILE_OK out-bytes=$OUT21a err: $(tr '\n' '|' < "$tmp/e21a" | head_bytes 160); arm b: rc=$RC21b out-bytes $OUT21b0->$OUT21b err: $(tr '\n' '|' < "$tmp/e21b" | head_bytes 160))"
fi
reset_clean "$C2" "$S8"

# --- 22 (i-4, the child deadline): a build child that out-runs the 30 s default
# M1203-i's finding i-4 (SV:1241, measured on the H9b gate port: a sweep child
# killed at 31/32 s): the reference's sh -c + make -j8 have NO deadline, but
# the port's rr_run_cmd -- the file's SINGLE jc_proc_capture site -- passed 0L,
# which include/jc_proc.h resolved to the unmeasured 30 s JC_PROC_DEF_TIMEOUT
# kill (landed 2026-06-25, 1370dd9b, with no measurement). The arm: the
# build-slow.sh stand-in SLEEPS 35 s (past 30, under the fix's named 600 s)
# before writing the two FINAL-line-shape --version binaries (the build-ok.sh
# shape) -- the UNFIXED verb SIGKILLs the child's group at the 30 s deadline
# (jc_proc_capture returns -2, rr_run_cmd's failure path), the build leg reads
# a failure, and the repin refuses rc 1 with the byte-exact cell-9 line `repin:
# the build failed: <log>` (measured, step-1 red: wall ~30 s; the kill lands
# mid-sleep so the build log stays empty -- the build leg still names it, which
# is the point: a -2 is a refusal, not a pass); the FIXED verb lets the child
# finish and the repin completes rc 0. The rest of the config is a success arm
# (the default doctor shims: hdoctor-ok.sh + live-ok.sh, both clean). The
# negative arm (out-running the NAMED 600 s bound) is NOT here -- run.sh's own
# deadline would kill this driver mid-sleep and manufacture a false red; the -2
# naming is pinned in code (rr_run_cmd's failure path) and in the operator's
# deadline API (SV:1249). The one 35 s sleep is what moves this driver to
# run.sh's 600-s list.
reset_clean "$C2" "$S8"
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-slow.sh"; CFG_DOC="$SHIM/hdoctor-ok.sh"; CFG_LIVE="$SHIM/live-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o22" "$tmp/e22"; RC22=$?
CUR22=$(readlink "$D/pins/current" 2>/dev/null)
if [ "$RC22" -eq 0 ] && [ "$CUR22" = "$S8" ] \
   && "$G" -qF "repin: done $S8, doctors clean" "$tmp/o22"; then
    t_ok "22: i-4 (the child deadline) -- the build stand-in sleeps 35 s (PAST the old unmeasured 30 s JC_PROC_DEF_TIMEOUT kill the unfixed capture's 0L resolved to) before writing the two FINAL-line-shape --version binaries; the child was NOT killed: the repin COMPLETES rc 0 with 'repin: done $S8, doctors clean' and pins/current -> $S8 (the build finished, the swap happened -- the reference's sh -c/make -j8 have no deadline, the fix's named bound is 600 s)"
else
    t_fail "22: i-4 expected rc 0 + 'repin: done $S8, doctors clean' + pins/current -> $S8 (the child out-ran the old 30 s default, so the unfixed verb kills it at wall ~30 s and refuses rc 1 at the build leg with the byte-exact cell-9 line 'repin: the build failed: <log>') (got rc=$RC22 current=$CUR22: $(tr '\n' '|' < "$tmp/e22" | head_bytes 160); last: $([ -f "$tmp/o22" ] && tr '\n' '|' < "$tmp/o22" | head_bytes 160))"
fi
reset_clean "$C2" "$S8"

# --- 23 (i-5): the exec-bit contract (the three copies keep the source's exec
# bit; the swapped jichi-harness runs). Cell 23 runs LAST (after cell 22's
# reset_clean), is self-contained (it sets its own fixture state and ends with
# reset_clean). The fixture's OLD pin pair and the running bin/ pair are written
# by build_drive as plain printf text files at 0644 (NOT executable -- a
# fixture slip: the real pins ARE the build's executable binaries). Cell 23
# chmods pins/<old>/* +x AFTER cell 22's reset_clean; bin/ is ALREADY 0755 into
# the cell -- cell 22's own success arm swapped the build-ok stand-in's
# chmod +x pair into it, and reset_clean's cp writes onto those EXISTING files
# (POSIX cp to an existing destination keeps the DESTINATION's pre-existing mode,
# not the source's) -- so the running pair the b12 backup snapshots is executable,
# as the real drive's is. (Latent fixture dependence named per the qwen27b review's
# finding 5: the two backup -x probes are fix-teeth conditioned on that 0755; a
# future refactor that left 0644 files in bin/ before cell 22's reset_clean would
# red them for a fixture reason.) The build-ok stand-in writes its output pair
# chmod +x, so every SOURCE the
# three copies read is executable. Then the repin S8 (the full success arm:
# default doctor shims, master C2, green ci) and the asserts:
# 23a the copies' mode: bin/jichi and bin/jichi-harness (post-swap) are -x;
#     pins/<s8>/jichi and pins/<s8>/jichi-harness are -x; and backups/bin-<old>/
#     jichi (+ harness) are -x (the snapshot of the executable running pair is
#     executable -- a restore from the backup would work).
# 23b the swapped harness ACTUALLY RUNS: bin/jichi-harness --version (< /dev/null)
#     exits 0 and its output carries build: <s8> (the build-ok stand-in's
#     three-line shape) -- the "doctors exec it" leg, the in-container stand-in
#     for the podman doctor that fails on a non-executable file.
# RED at the unfixed rr_cp: the repin SUCCEEDS (rc 0, the swap's cmp passes
# -- bytes are equal), but 23a fails: the three fresh destinations took 0644
# from fopen "wb" (umask), not the sources' 0755; 23b fails: bin/jichi-harness
# is not executable, the run answers Permission denied rc 126. GREEN after the
# fix: 23a all -x, 23b rc 0 + build: <s8>.
# The fclose teeth (the second half of i-5): WITHDRAWN as a driver cell. A
# short write that shows only at close needs a full disk or a fault the kernel
# does not expose to a user fixture, so no fixture can produce it
# deterministically on POSIX. The fclose check is pinned in the CODE (rr_cp's
# 1/0 contract: 1 means the bytes AND the close landed) and in the CHANGED
# line, read by the review -- a tooth the fixture cannot produce is a
# documented gap, not a fake green.
# Cell 23 fixture: make the old pins and running bin/ executable (the REAL
# shape: the real pins ARE the build's executable binaries).
chmod +x "$D/pins/$OLD8/jichi" "$D/pins/$OLD8/jichi-harness"
# Run the repin S8 (full success arm: default doctor shims).
CFG_PUSH="$PUSH"; CFG_CLONE=""; CFG_BUILD="$SHIM/build-ok.sh"; CFG_DOC="$SHIM/hdoctor-ok.sh"; CFG_LIVE="$SHIM/live-ok.sh"; cfg_build "$CFG"
run_repin "$CFG" "$S8" "$tmp/o23" "$tmp/e23"; RC23=$?

# 23a the copies' mode: all six files must be executable.
JBIN_X=0; [ -x "$D/bin/jichi" ] && JBIN_X=1
JH_X=0; [ -x "$D/bin/jichi-harness" ] && JH_X=1
JP_X=0; [ -x "$D/pins/$S8/jichi" ] && JP_X=1
JHP_X=0; [ -x "$D/pins/$S8/jichi-harness" ] && JHP_X=1
JBK_X=0; [ -x "$D/backups/bin-$OLD8/jichi" ] && JBK_X=1
JBKH_X=0; [ -x "$D/backups/bin-$OLD8/jichi-harness" ] && JBKH_X=1

# 23b the swapped harness actually runs: `--version` exits 0 and output carries `build: <s8>`.
"$D/bin/jichi-harness" --version < /dev/null > "$tmp/v23" 2>&1; RCV23=$?
OUT23=$(cat "$tmp/v23" 2>/dev/null)
BUILDL23=""; "$G" -qF "build: $S8" "$tmp/v23" 2>/dev/null && BUILDL23=1

# The t_ok line covering both arms: the contract states the three copies (pins,
# backup, the .new swap, repin.sh:57/:60/:65) keep the source's exec bit, and the
# swapped jichi-harness runs.
if [ "$RC23" -eq 0 ] && [ "$JBIN_X" -eq 1 ] && [ "$JH_X" -eq 1 ] && [ "$JP_X" -eq 1 ] && [ "$JHP_X" -eq 1 ] && [ "$JBK_X" -eq 1 ] && [ "$JBKH_X" -eq 1 ] && [ "$RCV23" -eq 0 ] && [ -n "$BUILDL23" ]; then
    t_ok "23: i-5 (the exec-bit contract) -- the three copies keep the source's exec bit: bin/jichi -x AND bin/jichi-harness -x AND pins/$S8/jichi -x AND pins/$S8/jichi-harness -x AND backups/bin-$OLD8/jichi -x AND backups/bin-$OLD8/jichi-harness -x; AND the swapped jichi-harness runs: bin/jichi-harness --version < /dev/null exits 0 AND its output carries 'build: $S8' (the build-ok stand-in's three-line shape) -- the contract: the three copies (pins, backup, the .new swap, repin.sh:57/:60/:65) keep the source's exec bit and the swapped jichi-harness is RUN by the cell (the in-container stand-in for the podman doctors that fail 'failed to exec pid1: Permission denied' on a non-executable file)"
else
    # The t_fail line names what it got (rc, the six -x probes as 1/0, the --version rc and its output, the stderr tail) in the driver's quoting idiom (tr '\n' '|' < file | head_bytes N).
    t_fail "23: i-5 expected rc 0 + the exec-bit contract (rc=$RC23; bin/jichi -x: $JBIN_X; bin/jichi-harness -x: $JH_X; pins/$S8/jichi -x: $JP_X; pins/$S8/jichi-harness -x: $JHP_X; backups/bin-$OLD8/jichi -x: $JBK_X; backups/bin-$OLD8/jichi-harness -x: $JBKH_X; --version rc: $RCV23; --version output: $(printf '%s' "$OUT23" | tr '\n' '|' | head_bytes 160); stderr tail: $(tr '\n' '|' < "$tmp/e23" 2>/dev/null | head_bytes 160))"
fi

# End the cell with reset_clean.
reset_clean "$C2" "$S8"

t_done
