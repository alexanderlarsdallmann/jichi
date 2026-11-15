#!/bin/sh
# smoke: `jichi-harness push run-drive --config <cfg>` -- the podman argv the
# host would build for the drive, PRINTED not run (M1147, batch H7; behaviour
# B5). The port of run-drive.sh as THE ARGV IT WOULD BUILD: dry by default
# (the command refuses to exec podman from inside this drive's container --
# the operator's rule "never run what may write to learn what it does"; a
# host install of the port would exec). It prints the argv as a SINGLE
# shell-quoted line (one quoted word per argument, the driver asserts per
# argument by grep, so the quoting is the driver's problem); every constant
# from run-drive.sh's header is in the argv (the limits 16/24g/16384, the
# caps, the pasta network with the loopback port 1234), every mount from the
# reference (the drive dir rw, conf ro, bin ro, the mirror's stored config
# copy ro, the model key file ro) and the reference's invariant: NO mount of
# the git TOKEN file, and the invocation ends with `drive --tenant lead`
# `< /dev/null`.
#
# M1147 (H7). Design note (the contract, behaviour list B5):
#   docs/internal/drive/2026-11-05-harness-port/notes/M1147.md
# Ported reference (read line by line; this driver holds B5):
#   docs/internal/drive/2026-10-27-hardening-host/run-drive.sh
#
# THE RED THIS IS HELD BY. On the tree before this batch the top-level verb
# `push` does not exist in jichi-harness: every check that runs a push
# subcommand gets `unknown command 'push'` and exit 2. All seven cells run
# the verb and are RED for exactly that reason (rc 2 instead of 0, and the
# argv line they assert is absent from stdout).
#
# WHAT IS CHECKED -- 7 cells, one t_ok/t_fail each:
#   1. `push run-drive --config <cfg>` (dry by default) exits 0 and prints
#      the argv as a single shell-quoted line on stdout: the line is
#      non-empty, starts with `podman run`, and every argument below is
#      grep-able as a QUOTED WORD (the driver's assertion mode -- quoting
#      keeps spaces and the `< /dev/null` tail from smearing the line).
#   2. every constant appears: `--network=pasta:-T,1234` (the host loopback
#      port 1234, LM Studio), `--cpus 16`, `--memory 24g`,
#      `--pids-limit 16384`, `--userns=keep-id`,
#      `--security-opt=no-new-privileges`, `--cap-drop=all`, `--rm`,
#      `--init` (the reference's constants and the container's fences).
#   3. the mounts: the drive dir rw (`-v <dir>:<dir>:rw`), conf ro, bin ro,
#      the mirror's stored config copy ro (mirror.git-config.harness-copy),
#      the model key file ro -- and NO line mounts the git TOKEN file: the
#      token path ($tmp/tokfile) is ABSENT from the whole output (the
#      reference's invariant: the container never holds the token; the host
#      pushes, the timer does it).
#   4. the env: the four `-e NAME=value` pairs `HOME=<dir>/home`, `TMPDIR=/tmp`,
#      `TZ=Europe/Berlin` (and `PATH=`) -- and the NEGATIVE (def-382, red first):
#      none of `<dir>/home`, `/tmp`, `Europe/Berlin` may ALSO stand alone as a
#      bare quoted token after its pair (podman would read the first stray path
#      as the image name).
#   5. the invocation ends with `drive --tenant lead` and `< /dev/null`
#      (the note's invariant: the container's stdin is never the host's).
#   6. the image: `host.image` (jichi-drive:test) appears in the argv (the
#      argv position of the image, before the harness invocation).
#   7. a config WITHOUT the host block: `push run-drive` refuses exit 2,
#      naming `no host block` (the host commands are host-side by
#      construction; a container config that names no host dir must not
#      guess one).
#
# THE FIXTURE. Under $SMOKE_TMP: a minimal harness config (dir, modelKeyFile
# pointing at a canary key file, gitTokenFile pointing at a DISTINCT fake
# 40-hex canary token, drive.branchPrefix, the note's host block: pushHome
# under $SMOKE_TMP, image jichi-drive:test) and a second config without the
# host block. The drive dir is laid out by hand (conf/ and bin/ as the
# reference's argv names them) -- no git repos, no podman, no network. The
# red tree's loader parses point-wise and ignores the unknown host block,
# which is why the red lands on `unknown command 'push'`, not on a config
# refusal.
#
# Runs the tree's binary by path ($HBIN, the third_program idiom), never a
# PATH-resolved jichi-harness, and -- for smoke_lint check 5 -- also runs
# the tree's "$BIN" once. No timeout wrapper; git children (none here) would
# carry -c user.name/-c user.email (M2-neutral).
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)

# smoke_lint check 5: there are no fixture git children here, but $BIN must
# be exercised so the lint sees the driver validate the just-built tree.
"$BIN" --version > "$tmp/binv" 2>/dev/null

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"

# Neutralise ambient harness loader settings (env wins over the config).
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE

KEYCANARY=$(od -An -N20 -tx1 /dev/urandom 2>/dev/null | tr -d ' \n'); [ -n "$KEYCANARY" ] || KEYCANARY="canarykey$$_"
TOKEN_CANARY="d4e0a6b9c3f82571d0e4a7b2c9f5e813a6d0b4e8"

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

printf '%s\n' "$KEYCANARY" > "$tmp/kfile"; chmod 600 "$tmp/kfile"
printf '%s\n' "$TOKEN_CANARY" > "$tmp/tokfile"; chmod 600 "$tmp/tokfile"

# The drive dir the argv mounts (the reference's $D shape, under $tmp):
# conf/ (the harness config, ro) and bin/ (the pinned binaries, ro).
DRV="$tmp/drive"; mkdir -p "$DRV/conf" "$DRV/bin" || fixture_fail "drive dir"
printf 'conf: the harness config (smoke fixture)\n' > "$DRV/conf/harness.json"
printf 'bin: the pinned jichi + jichi-harness (smoke fixture)\n' > "$DRV/bin/README"
PH="$tmp/pushhome"; mkdir -p "$PH" || fixture_fail "pushHome"

cat > "$tmp/cfg.json" <<EOF
{"dir":"$DRV",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"drive":{"branchPrefix":"drive/x-test","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"host":{"pushHome":"$PH","image":"jichi-drive:test"}}
EOF
# cfgnohost: the SAME config without the host block (cell 7).
cat > "$tmp/cfgnohost.json" <<EOF
{"dir":"$DRV",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"drive":{"branchPrefix":"drive/x-test","gitName":"Harness Test","gitEmail":"harness-test@example.com"}}
EOF

# One dry run; the assertion target is its stdout (the single argv line).
"$HBIN" push run-drive --config "$tmp/cfg.json" \
    < /dev/null > "$tmp/r1" 2> "$tmp/r1.err"
rc1=$?
LINE=$(head -1 "$tmp/r1" 2>/dev/null)
# The dependent cells (2-6) read $LINE: where the verb produced nothing (the
# red tree), their assertions would fail on an empty line for the wrong
# reason. $REDLINE carries the primary red evidence so every cell can quote
# it; a non-red tree that empties stdout for its own reasons still fails
# here, but on its own assertions.
REDLINE=""
if [ -z "$LINE" ] && tr '\n' ' ' < "$tmp/r1.err" 2>/dev/null | grep -q "unknown command"; then
    REDLINE=" (the \`push\` verb produced no argv line: $(tr '\n' ' ' < "$tmp/r1.err" | head_bytes 160))"
fi

# qword STR -- grep $LINE for STR as a QUOTED WORD (the single-quoted
# argument form the driver expects; a quoted argument with no spaces inside
# also satisfies it, so the assertion is per argument, quoting-agnostic
# where the reference's words carry no spaces).
qword() {
    printf '%s\n' "$LINE" | grep -qF "\"$1\"" || printf '%s\n' "$LINE" | grep -qF "'$1'"
}

# qword_bare STR -- grep $LINE for STR as a STANDALONE single-quoted word: the
# value carrying its OWN leading quote, followed by a space (`'<value>' `). The
# line is single-quoted (build_rundrive_line appends ` '<token>'` per argument),
# so a bare token is `'value' `, while the tail of a `-e NAME=value` pair has no
# quote before the value (the pair is `'NAME=value'`). The bare form is the
# defect (def-382): after `-e HOME=<dir>/home` the stray `<dir>/home` stands
# alone and podman would read it as the IMAGE name.
qword_bare() {
    printf '%s\n' "$LINE" | grep -qF "'$1' "
}

# --- 1: the dry print (exit 0, the single shell-quoted argv line) ------------
if [ "$rc1" -ne 0 ]; then
    t_fail "1: \`push run-drive --config <cfg>\` (dry by default) exited $rc1 (want 0) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/r1.err" | head_bytes 200)"
elif [ -z "$LINE" ]; then
    t_fail "1: the dry run exited 0 but printed no argv line on stdout: $(tr '\n' '|' < "$tmp/r1" | head_bytes 200)"
elif ! printf '%s\n' "$LINE" | grep -qF "podman run"; then
    t_fail "1: the printed argv line does not start with \`podman run\` -- the port prints THE ARGV IT WOULD BUILD: $(printf '%s' "$LINE" | head_bytes 200)"
else
    t_ok "1: \`push run-drive\` (dry by default) exits 0 and prints the argv as a single shell-quoted line starting with \`podman run\` (it does NOT exec podman -- the operator's rule)"
fi

# --- 2: every constant (limits, caps, the pasta network) ---------------------
if [ -z "$LINE" ]; then
    t_fail "2: no argv line to check the constants against (cell 1 did not land): $(tr '\n' ' ' < "$tmp/r1.err" | head_bytes 160)"
else
    _bad=""
    qword "--network=pasta:-T,1234" || _bad="$_bad --network=pasta:-T,1234"
    qword "--cpus" || _bad="$_bad --cpus"
    qword "16" || _bad="$_bad 16"
    qword "--memory" || _bad="$_bad --memory"
    qword "24g" || _bad="$_bad 24g"
    qword "--pids-limit" || _bad="$_bad --pids-limit"
    qword "16384" || _bad="$_bad 16384"
    qword "--userns=keep-id" || _bad="$_bad --userns=keep-id"
    qword "--security-opt=no-new-privileges" || _bad="$_bad --security-opt=no-new-privileges"
    qword "--cap-drop=all" || _bad="$_bad --cap-drop=all"
    qword "--rm" || _bad="$_bad --rm"
    qword "--init" || _bad="$_bad --init"
    if [ -n "$_bad" ]; then
        t_fail "2: the argv line is missing constants:$_bad (run-drive.sh's header: the old drive's limits, the fences, the pasta loopback port 1234)${REDLINE}: $(printf '%s' "$LINE" | head_bytes 300)"
    else
        t_ok "2: every constant appears -- --network=pasta:-T,1234, --cpus 16, --memory 24g, --pids-limit 16384, --userns=keep-id, --security-opt=no-new-privileges, --cap-drop=all, --rm, --init"
    fi
fi

# --- 3: the mounts (rw dir, ro conf/bin/stored-copy/key, NO token mount) -----
if [ -z "$LINE" ]; then
    t_fail "3: no argv line to check the mounts against (cell 1 did not land): $(tr '\n' ' ' < "$tmp/r1.err" | head_bytes 160)"
else
    _bad=""
    qword "$DRV:$DRV:rw" || _bad="$_bad the-drive-dir-rw"
    qword "$DRV/conf:$DRV/conf:ro" || _bad="$_bad conf-ro"
    qword "$DRV/bin:$DRV/bin:ro" || _bad="$_bad bin-ro"
    qword "$DRV/mirror.git-config.harness-copy:$DRV/mirror.git-config.harness-copy:ro" || _bad="$_bad the-mirror-stored-config-copy-ro"
    qword "$tmp/kfile:$tmp/kfile:ro" || _bad="$_bad the-model-key-file-ro"
    if [ -n "$_bad" ]; then
        t_fail "3: the argv line is missing mounts:$_bad (run-drive.sh's overlay set: the drive dir rw, conf/bin and the stored config copy ro on top, the model key ro)${REDLINE}: $(printf '%s' "$LINE" | head_bytes 300)"
    elif printf '%s\n' "$tmp/r1" | grep -qF "$tmp/tokfile"; then
        t_fail "3: the git TOKEN file ($tmp/tokfile) IS MOUNTED in the argv -- the reference's invariant: NO token mount (drive.teamPush is false: the container never holds the token, the host pushes)"
    else
        t_ok "3: the mounts -- the drive dir rw, conf ro, bin ro, mirror.git-config.harness-copy ro, the model key file ro -- and NO mount of the git token file (the token path is absent from the whole output: the invariant)"
    fi
fi

# --- 4: the env (HOME, TMPDIR, TZ) -------------------------------------------
if [ -z "$LINE" ]; then
    t_fail "4: no argv line to check the env against (cell 1 did not land): $(tr '\n' ' ' < "$tmp/r1.err" | head_bytes 160)"
else
    _bad=""
    # The four -e NAME=value pairs, asserted as the full quoted pair (the
    # reference's line carries only the pair form; the bare value is the
    # defect, not a separate argument). Each pair is a standalone quoted word
    # in the line, so qword on the full NAME=value form holds on both trees.
    qword "HOME=$DRV/home" || _bad="$_bad HOME=<dir>/home"
    qword "-e" || _bad="$_bad -e"
    qword "TMPDIR=/tmp" || _bad="$_bad TMPDIR=/tmp"
    qword "TZ=Europe/Berlin" || _bad="$_bad TZ=Europe/Berlin"
    qword "PATH=$DRV/bin:/usr/local/bin:/usr/bin:/bin" || _bad="$_bad PATH=<dir>/bin:..."
    # The NEGATIVE (def-382's tooth, red first): the bare values must NOT stand
    # alone as their own quoted tokens. The -e NAME=value pairs carry each value
    # (the positive half above is green on both trees); on the unfixed tree the
    # port ALSO appends the bare <dir>/home, /tmp and Europe/Berlin after each
    # pair, and podman reads that first stray directory path as the IMAGE name.
    qword_bare "$DRV/home" && _bad="$_bad bare-<dir>/home"
    qword_bare "/tmp" && _bad="$_bad bare-/tmp"
    qword_bare "Europe/Berlin" && _bad="$_bad bare-Europe/Berlin"
    if [ -n "$_bad" ]; then
        t_fail "4: the argv line is missing env values or carries a bare env value:$_bad (run-drive.sh's -e set is FOUR -e NAME=value pairs -- HOME, TMPDIR, TZ, PATH -- with NO bare value between or after them)${REDLINE}: $(printf '%s' "$LINE" | head_bytes 300)"
    else
        t_ok "4: the env -- the four -e NAME=value pairs HOME=<dir>/home, TMPDIR=/tmp, TZ=Europe/Berlin, PATH=<dir>/bin:... -- with NO bare value standing alone (def-382)"
    fi
fi

# --- 5: the invocation tail (drive --tenant lead, `< /dev/null`) --------------
if [ -z "$LINE" ]; then
    t_fail "5: no argv line to check the tail against (cell 1 did not land): $(tr '\n' ' ' < "$tmp/r1.err" | head_bytes 160)"
else
    _bad=""
    qword "drive" || _bad="$_bad drive"
    qword "--tenant" || _bad="$_bad --tenant"
    qword "lead" || _bad="$_bad lead"
    qword "< /dev/null" || _bad="$_bad '< /dev/null'"
    if [ -n "$_bad" ]; then
        t_fail "5: the argv line does not end the invocation with \`drive --tenant lead\` + \`< /dev/null\`:$_bad (the note's invariant: the container's stdin is never the host's)${REDLINE}: $(printf '%s' "$LINE" | head_bytes 300)"
    else
        t_ok "5: the invocation ends with \`drive --tenant lead\` and \`< /dev/null\` (the note's invariant: the container never reads the host's stdin)"
    fi
fi

# --- 6: the image (host.image) ------------------------------------------------
if [ -z "$LINE" ]; then
    t_fail "6: no argv line to check the image against (cell 1 did not land): $(tr '\n' ' ' < "$tmp/r1.err" | head_bytes 160)"
else
    if ! qword "jichi-drive:test"; then
        t_fail "6: the argv line does not carry the image from host.image (jichi-drive:test)${REDLINE}: $(printf '%s' "$LINE" | head_bytes 300)"
    else
        t_ok "6: the image comes from host.image -- jichi-drive:test appears in the argv"
    fi
fi

# --- 7: a config WITHOUT the host block refuses, naming it -------------------
"$HBIN" push run-drive --config "$tmp/cfgnohost.json" \
    < /dev/null > "$tmp/r7" 2> "$tmp/r7.err"
rc7=$?
if [ "$rc7" -ne 2 ]; then
    t_fail "7: \`push run-drive\` with a config that has NO host block exited $rc7 (want 2) -- the host commands must not guess a host dir: $(tr '\n' ' ' < "$tmp/r7.err" | head_bytes 200)"
elif ! grep -qiE "no host block" "$tmp/r7.err"; then
    t_fail "7: the refusal does not name \`no host block\` (on the red tree the line is \`unknown command 'push'\`, not the host-block requirement): $(tr '\n' ' ' < "$tmp/r7.err" | head_bytes 200)"
else
    t_ok "7: a config without the host block is refused exit 2, naming \`no host block\` (the host commands are host-side by construction)"
fi

t_done
