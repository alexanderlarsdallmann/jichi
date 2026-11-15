#!/bin/sh
# smoke: `jichi-harness notify --config <cfg> [--inbox <dir>] <SHA8> <TEXT> [ATTACH]`
# -- deliver a host-gate verdict (M1152, batch H8; the C89 port of the
# 2026-10-27 hardening-host drive's gate-note.sh). A verdict reaches its two
# readers "no session needed": the LEAD (a note in the drive's inbox/, and,
# when a run is LIVE, an inject of the note body through that run's control
# socket `<dir>/bin/jichi control <ctl/lead.sock> inject <body>`) and the
# OPERATOR (a line appended to <gateHome>/digest.md and a config-named
# notification command hook that gets the DIGEST PATH).
#
# M1152 (H8). Design note (the contract, the behaviour list B1-B11):
#   docs/internal/drive/2026-11-05-harness-port/notes/M1152.md
# Ported reference (read line by line; this driver holds B1-B7, B9-B10):
#   docs/internal/drive/2026-10-27-hardening-host/gate-note.sh
#
# THE RED THIS IS HELD BY. On the tree before the change the top-level verb
# `notify` does not exist in jichi-harness: every cell that runs it gets
# `unknown command 'notify'` (exit 2), and every note / digest / hook /
# inject assertion it makes is absent. This is the port's FIRST test, red by
# construction -- exactly as the `push` subcommands' driver
# (harness_push_tick) was. The red tree's loader parses the config point-wise
# and IGNORES the unknown `host` keys gateHome/notifyCommand, so the red
# lands on the unknown-command refusal -- NOT on a config refusal (notes/
# M1152.md, "Run 0024 confirmations": a config that sets BOTH keys must not
# make doctor or the loader fail).
#
# M1152-i (H9a, the H8 follow-up): three driver teeth added/changed, one code
# byte fixed (see docs/internal/drive/2026-11-05-harness-port/LOG.md 03:56 run
# 0026, the WEAK-finding verdict):
#   1. cell 1's shape test is parenthesised -- POSIX sh gives && and || EQUAL
#      precedence, left-associative, so the un-parenthesised `!A || !B && !C ||
#      !D` parsed as `((!A || !B) && !C) || !D`: a broken TITLE with correct
#      timestamp+records recorded t_ok (the title check was MASKED).
#   3e. a PLAIN FILE at the newest run's lead.sock path must NOT inject (the
#      S_ISSOCK half, jc_harness_notify.c:718 -- cell 3c only covers an ABSENT
#      socket).
#   3f. a TRAILING newline of the inject reply must not survive the flatten: the
#      reference's o=$(... 2>&1) strips it BEFORE `tr '\n' ' '` (gate-note.sh:26),
#      so `{"ok":true}\n` carries as `{"ok":true}`, not `{"ok":true} ` with a
#      trailing space (the code strip is M1152-i's; cell 8's ONE SPACE PER
#      internal newline stays untouched).
#
# WHAT IS CHECKED -- 9 cells, one t_ok/t_fail each:
#   1. the note by effect (B1). NO --inbox (the REAL drive inbox
#      <dir>/inbox): a single note file <dir>/inbox/<stamp>-gate-00000000.md
#      -- the reference's gate-note.sh:12 is s=${1:?usage: gate-note.sh SHA8
#      TEXT}, and the :? is the error-if-unset form, so $s is the FULL sha8
#      (the filename, the title, the records line, the digest line and the
#      watch line all carry the full sha). The M1152 note's s=${1:7} was a
#      misread of that line; corrected in the wave-b2 run. The body is the
#      behaviour-1 shape: the `# Host gate: 00000000` title, the
#      `Host gate (automatic, <now>): <TEXT>` line, and the `Records on the
#      host: <gate>/00000000/ (the supervisor reads them; you need not).` line.
#      M1152-i: the shape test is PARENTHETISED per term -- POSIX sh gives &&
#      and || EQUAL precedence, left-associative, so the un-parenthesised
#      `!A || !B && !C || !D` (A=title, B=exact timestamp, C=wildcard
#      timestamp, D=records) parsed as `((!A || !B) && !C) || !D`: a note with
#      a broken TITLE but correct timestamp+records evaluated to false and
#      recorded t_ok (the title half was masked -- the run-0026 WEAK finding,
#      reproduced in /tmp on the verbatim condition). Each half is now its own
#      tooth, so a broken title, a broken timestamp (both greps) or a broken
#      records line each t_fail on their own.
#      The inject FIRES (runs/0008 is live) -- the pinned shim is recorded.
#      The <now> stamp is not asserted to the second: the driver captures it
#      with date and greps the flexible minute.
#   2. REHEARSAL + the inject gate (B3, B9). WITH --inbox <scratch>: the
#      note in the scratch inbox carries the REHEARSAL prefix and the inject
#      is SKIPPED (the shim's arg file is unchanged from cell 1).
#   3. the live-run detection, all three halves (B4, B5): 3a fire (newest
#      run live, a REAL socket via the sockbind tool, no scratch inbox -> the
#      shim gets `control <sock> inject <body>`), 3b end= kills it, 3c no
#      socket kills it, 3d the scratch inbox kills it, 3e a PLAIN FILE at
#      the socket path kills it (M1152-i, the S_ISSOCK half), 3f a reply that
#      ends in a newline carries NO trailing space in the watch line (M1152-i,
#      the reference's o=$(... 2>&1) strip).
#   4. the digest line (B7). Two notifies -> <gateHome>/digest.md carries
#      two `<now> gate <s>: <text>` lines in order.
#   5. the hook, CHANGED shape (B6). A config-named command that records its
#      args: it runs with the DIGEST PATH (not the TEXT); a failing hook
#      (rc 3) never changes notify's rc (still 0); an ABSENT hook runs
#      nothing and errors nothing.
#   6. ATTACH (B1, B10). A non-empty attach file adds a `## Attached:
#      <basename>` section + its contents; an EMPTY and a MISSING path add
#      no section (the reference's [ -s ] test).
#   7. the usage refusal (B10). NO positional args -> exit 2, the reference's
#      ${1:?} usage, and NO note is written.
#   8. the flatten carries the bytes (the review fix). A blank line in the
#      inject reply is flattened ONE SPACE PER NEWLINE (the reference's
#      tr '\n' ' ' | cut -c1-160), NO coalescing -- "line1\n\nline3" becomes
#      "line1  line3" (two spaces). The carried bytes are asserted on the
#      stdout watch line.
#   9. the over-cap refusal (CHANGED, the review fix). The reference cats the
#      ATTACH and prints the TEXT with NO bound; the port BOUNDS the TEXT
#      (NOTIFY_TEXT_CAP, 128 KB) and the ATTACH (NOTIFY_ATTACH_CAP, 1 MB) as a
#      memory bound and REFUSES over-cap input with a named exit-1 before any
#      write. A 131071-byte TEXT (the kernel's argv limit -- a longer TEXT dies
#      in the exec with E2BIG before notify runs) and a 2 MB ATTACH each refuse,
#      name the cap and the actual size on stderr, and write NO note / digest
#      line.
#
# THE FIXTURE. Under $tmp: a scratch <dir>/ (the harness's drive dir) with
# inbox/, runs/0007/ctl/, runs/0008/ctl/ and gate/ (the digest home, named by
# host.gateHome). A FAKE pinned jichi at <dir>/bin/jichi records its args
# (one per line) to $tmp/shim-args and answers `{"ok":true}`; the reference's
# inject argv is <dir>/bin/jichi control <sock> inject <body> < /dev/null.
# modelKeyFile/gitTokenFile are random-hex canary files (never a real key).
# host.notifyCommand is a driver-written sh stub that appends each of its
# args to $tmp/hook-args, so the driver can assert the hook ran with the
# digest path. The driver runs the tree's binary by path ($HBIN, the
# third_program idiom), never a PATH-resolved jichi-harness, and -- for
# smoke_lint check 5 -- also runs the tree's "$BIN" once. No timeout wrapper
# (bounded one-shot binds and local file writes).
. "$(dirname "$0")/_smoke.sh"

t_plan 9
smoke_home
tmp=$(smoke_tmp)

# smoke_lint check 5: the fixture needs no git, but $BIN must be exercised
# so the lint sees the driver validate the just-built tree.
"$BIN" --version > "$tmp/binv" 2>/dev/null

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"
SOCKBIND="$ROOT/tests/tools/sockbind"

# Neutralise ambient harness loader settings (env wins over the config).
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE

# A fixture failure emits one t_fail per cell so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt 9 ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

# bind_sock PATH LABEL -- create a real AF_UNIX socket via sockbind, KEEPING the
# evidence (rc + stderr) so a fixture failure names its cause. A bind failure is
# an instrument problem (stale path, over-long path), not a product red. SV:2138.
bind_sock() {
    # $1 = the short REAL path, $2 = the display/symlink path, $3 = the label
    _bs_err=$( "$SOCKBIND" "$1" 2>&1 ); _bs_rc=$?
    if [ "$_bs_rc" -ne 0 ]; then
        fixture_fail "sockbind $3: rc=$_bs_rc: $_bs_err (real=$1 link=$2)"
    fi
    rm -f "$2"
    ln -s "$1" "$2" || fixture_fail "symlink $3: $2 -> $1"
}
# bind_sock_soft REAL LINK -- the driver's restore: a re-bind that may
# legitimately hit EADDRINUSE (the socket already exists); the result is
# ignored (a present socket is fine), stderr discarded, no plan effect.
bind_sock_soft() {
    # $1 = real, $2 = link (the restore; a present socket is fine)
    "$SOCKBIND" "$1" 2>/dev/null || :
    rm -f "$2"; ln -s "$1" "$2" 2>/dev/null || :
}

if [ ! -x "$HBIN" ]; then
    fixture_fail "the harness binary $HBIN is not built (run `make jichi-harness` first)"
fi
if [ ! -x "$SOCKBIND" ]; then
    fixture_fail "the sockbind tool $SOCKBIND is not built (run `make tests/tools/sockbind` first)"
fi

DIR="$tmp/dir"
# SV:2138 (measured 2026-11-08): the real socket is bound at the SHORT
# path $tmp/l8.sock -- this drive runs the tier with a 70-char TMPDIR, which puts
# $DIR/runs/0008/ctl/lead.sock at 123 bytes > sun_path (108); the old sockbind
# TRUNCATED the path, so cell 3d re-bound the still-live truncated path and
# failed EADDRINUSE (the "Address already in use" that looked like a deadline).
# The expected location is a SYMLINK to the short path: -S follows the link, and
# a connect through it resolves the short real path.
S8="$DIR/runs/0008/ctl/lead.sock"
S8R="$tmp/l8.sock"
mkdir -p "$DIR/inbox" "$DIR/runs/0007/ctl" "$DIR/runs/0008/ctl" "$DIR/gate" \
    "$DIR/bin" "$tmp/scratch-inbox" || fixture_fail "scratch dirs"

# The fake pinned jichi: records its args one per line to $tmp/shim-args and
# answers the control client with {"ok":true}. It is <dir>/bin/jichi, the
# home the reference's $H/pins/current/jichi translates to. The cells assert
# the ARGV (the behaviour list's "the driver proves the ARGV by effect").
{
    printf '#!/bin/sh\n'
    printf 'for _a in "$@"; do printf '\''%%s\\n'\'' "$_a" >> %s/shim-args; done\n' "$tmp"
    printf 'echo "{\\"ok\\":true}"\n'
    printf 'exit 0\n'
} > "$DIR/bin/jichi" || fixture_fail "fake jichi shim"
chmod 755 "$DIR/bin/jichi"
: > "$tmp/shim-args"

KEYCANARY=$(od -An -N20 -tx1 /dev/urandom | tr -d ' \n'); [ -n "$KEYCANARY" ] || KEYCANARY="canarykey$$_"
TOKCANARY=$(od -An -N20 -tx1 /dev/urandom | tr -d ' \n'); [ -n "$TOKCANARY" ] || TOKCANARY="canarytok$$_"
printf '%s\n' "$KEYCANARY" > "$tmp/kfile"; chmod 600 "$tmp/kfile"
printf '%s\n' "$TOKCANARY" > "$tmp/tokfile"; chmod 600 "$tmp/tokfile"

# The operator hook stub: appends each argument, one per line, to
# $tmp/hook-args, then exits 0.
printf '#!/bin/sh\nfor _a in "$@"; do printf '\''%%s\\n'\'' "$_a" >> %s/hook-args; done\nexit 0\n' \
    "$tmp" > "$tmp/hook.sh" || fixture_fail "hook stub"
chmod 755 "$tmp/hook.sh"
: > "$tmp/hook-args"

# The config: dir, the two canary secret files, and the host block (gateHome
# is the digest home; notifyCommand is the hook). The red tree's loader reads
# this point-wise and ignores the two unknown host keys, so the red is the
# unknown-command refusal, not a config refusal.
cat > "$tmp/cfg.json" <<EOF
{"dir":"$DIR",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"host":{"gateHome":"$DIR/gate","notifyCommand":"$tmp/hook.sh"}}
EOF

# runs/0007 is a FINISHED run (meta carries end=); runs/0008 is LIVE (meta
# has no end=). The inject gate is the reference's three-half test: newest by
# numeric sort AND ctl/lead.sock is a REAL socket (the -S test) AND no end=
# AND no scratch inbox. Cell 3 drives all three halves; until then 0008 has
# NO socket, so the inject does not fire (a correct "no live run").
printf 'rc=0\nend=2026-11-05T07:14:00Z\n' > "$DIR/runs/0007/meta.txt" || fixture_fail "0007 meta"
printf 'rc=0\n' > "$DIR/runs/0008/meta.txt" || fixture_fail "0008 meta"

# newest_gate_note INBOX -- the most-recently-written <stamp>-gate-*.md in
# INBOX (ls -t; a glob alone would be order-unstable across the driver's
# many cells). Used by the ATTACH cells, which must read THEIR note, not a
# neighbour's.
newest_gate_note() {
    ls -t "$1"/*-gate-*.md 2>/dev/null | head -n 1
}

# --- 1: the note by effect (the real drive inbox, NO --inbox) ----------------
now=$(date '+%F %T %z')
"$HBIN" --config "$tmp/cfg.json" notify 00000000 'GREEN on BATCH H8 READY' \
    < /dev/null > "$tmp/c1.out" 2> "$tmp/c1.err"
rc1=$?
# The 8-hex SHA: the reference's gate-note.sh:12 is s=${1:?} (the :? is the
# error-if-unset form, NOT a substring), so $s is the FULL sha8 and the note
# file is <stamp>-gate-00000000.md.
nnotes=$(ls "$DIR/inbox" 2>/dev/null | grep -c '\-gate-00000000\.md$')
[ -n "$nnotes" ] || nnotes=0
_note=""
[ "$nnotes" -eq 1 ] && _note=$(ls "$DIR"/inbox/*-gate-00000000.md 2>/dev/null | head -n 1)
if [ "$rc1" -ne 0 ]; then
    t_fail "1: notify over the real inbox exited $rc1 (want 0) -- on the red tree the \`notify\` verb does not exist: $(tr '\n' ' ' < "$tmp/c1.err" | head_bytes 200)"
elif [ "$nnotes" -ne 1 ]; then
    t_fail "1: the real inbox holds $nnotes note(s) matching *-gate-00000000.md (want exactly 1) -- the note was not written: $(ls "$DIR/inbox" 2>/dev/null | tr '\n' ' ')"
elif [ -z "$_note" ]; then
    t_fail "1: the single *-gate-00000000.md note in the real inbox could not be read: $(ls "$DIR/inbox" 2>/dev/null | tr '\n' ' ')"
elif ! { head -n 1 "$_note" 2>/dev/null | grep -q '^# Host gate: 00000000$'; } \
     || ! { grep -q '^Host gate (automatic, '"$now"'): GREEN on BATCH H8 READY$' "$_note" 2>/dev/null \
            || grep -q '^Host gate (automatic, .*)\+: GREEN on BATCH H8 READY$' "$_note" 2>/dev/null; } \
     || ! { grep -qF "Records on the host: $DIR/gate/00000000/ (the supervisor reads them; you need not)." "$_note" 2>/dev/null; }; then
    t_fail "1: the note body is not the behaviour-1 shape (title / Host gate (automatic, ...): <text> / Records on the host line -- each term parenthesised, M1152-i: un-parenthesised the && and || shared left-associative precedence and a broken TITLE with correct timestamp+records recorded t_ok): $(tr '\n' '|' < "$_note" 2>/dev/null | head_bytes 300)"
else
    t_ok "1: the note by effect -- the real inbox holds exactly one <stamp>-gate-00000000.md (the FULL sha8 filename, the reference's s=\${1:?}) whose body is the reference's shape: # Host gate: 00000000, the Host gate (automatic, ...): GREEN line, and the Records on the host: <gate>/00000000/ line"
fi

# --- 2: REHEARSAL + the inject gate (WITH --inbox scratch) -------------------
_shim_after1=$(cat "$tmp/shim-args" 2>/dev/null)
now=$(date '+%F %T %z')
"$HBIN" --config "$tmp/cfg.json" notify --inbox "$tmp/scratch-inbox" 00000000 'REH' \
    < /dev/null > "$tmp/c2.out" 2> "$tmp/c2.err"
rc2=$?
_snote=$(newest_gate_note "$tmp/scratch-inbox")
_shim_after2=$(cat "$tmp/shim-args" 2>/dev/null)
if [ "$rc2" -ne 0 ]; then
    t_fail "2: the scratch-inbox notify exited $rc2 (want 0) -- on the red tree the \`notify\` verb does not exist: $(tr '\n' ' ' < "$tmp/c2.err" | head_bytes 200)"
elif [ -z "$_snote" ] || ! grep -qF 'REHEARSAL (scratch inbox, nothing is delivered to the lead) -- Host gate (automatic, ' "$_snote" 2>/dev/null; then
    t_fail "2: the scratch inbox has no note carrying the REHEARSAL prefix: $(ls "$tmp/scratch-inbox" 2>/dev/null | tr '\n' ' ') | $(tr '\n' '|' < "$_snote" 2>/dev/null | head_bytes 200)"
elif [ "$_shim_after2" != "$_shim_after1" ]; then
    t_fail "2: the scratch inbox changed the shim's arg file (the inject must be SKIPPED when a scratch inbox is set) -- the inject fired against a rehearsal: $(tr '\n' ' ' < "$tmp/shim-args" 2>/dev/null | head_bytes 200)"
else
    t_ok "2: REHEARSAL + the inject gate -- the scratch inbox note carries the REHEARSAL prefix and the shim's arg file is unchanged (the scratch inbox suppresses the inject)"
fi
# Reset the shim arg file for the cells that will check it.
: > "$tmp/shim-args"

# --- 3: the live-run detection, all three halves -----------------------------
# 3a: fire. runs/0008 is the newest run, live (no end=), and given a REAL
# socket via sockbind; no scratch inbox -> the inject fires and the pinned
# shim records `control <sock> inject <body>`.
bind_sock "$S8R" "$S8" 0008
now=$(date '+%F %T %z')
"$HBIN" --config "$tmp/cfg.json" notify 11111111 'LIVE' < /dev/null \
    > "$tmp/c3a.out" 2> "$tmp/c3a.err"
rc3a=$?
_3a_ok=0; _3a_msg=""
if [ "$rc3a" -ne 0 ]; then
    _3a_msg="3a rc=$rc3a (want 0)"
elif ! { grep -qx 'control' "$tmp/shim-args" && grep -qxF "$DIR/runs/0008/ctl/lead.sock" "$tmp/shim-args" && grep -qx 'inject' "$tmp/shim-args"; } 2>/dev/null; then
    _3a_msg="3a shim argv lacks control / <sock> / inject: $(tr '\n' ' ' < "$tmp/shim-args" 2>/dev/null | head_bytes 200)"
elif ! grep -qF 'Host gate (automatic,' "$tmp/shim-args" 2>/dev/null; then
    _3a_msg="3a the inject body (the note line) was not passed: $(tr '\n' ' ' < "$tmp/shim-args" 2>/dev/null | head_bytes 200)"
# FINDING 3 (the review fix): the CHANGED watch line's carried bytes are now
# asserted. Its home is STDOUT (not a watch.out file), and its shape is
#   ^<now> gate <s>: note <basename>; (no live run|run <N> inject rc=<rc> ...)
# where <now> is the date '+%F %T %z' stamp, <s> the sha8, <basename> the
# note's filename. A 3a live run (ctl socket present, no end=) records
# "run <N> inject rc=...". The teeth: "gate 11111111:", "note *-gate-11111111.md;",
# and the line being on stdout at all.
elif ! grep -qE '^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} [+-][0-9]{4} gate 11111111: note [0-9]+-[0-9]+-gate-11111111\.md; (no live run|run [0-9]+ inject rc=[0-9-]+ .*)' "$tmp/c3a.out" 2>/dev/null; then
    _3a_msg="3a the stdout watch line is not the carried shape ^<now> gate 11111111: note <stamp>-gate-11111111.md; <inj>: $(tr '\n' '|' < "$tmp/c3a.out" 2>/dev/null | head_bytes 200)"
else
    _3a_ok=1
fi
# 3b: end= kills it. Add end= to the newest run's meta; the inject must NOT fire.
printf 'end=2026-11-05T08:00:00Z\n' >> "$DIR/runs/0008/meta.txt" || fixture_fail "0008 end="
: > "$tmp/shim-args"
"$HBIN" --config "$tmp/cfg.json" notify 22222222 'LIVE' < /dev/null \
    > "$tmp/c3b.out" 2> "$tmp/c3b.err"
rc3b=$?
_3b_ok=0; _3b_msg=""
if [ "$rc3b" -ne 0 ]; then
    _3b_msg="3b rc=$rc3b (want 0)"
elif [ -s "$tmp/shim-args" ]; then
    _3b_msg="3b the inject fired although the newest run has end= (it must not): $(tr '\n' ' ' < "$tmp/shim-args" 2>/dev/null | head_bytes 160)"
else
    _3b_ok=1
fi
# 3c: no socket kills it. Remove the end= line (live again) and rm the socket.
grep -v '^end=' "$DIR/runs/0008/meta.txt" > "$tmp/c3c.tmp" 2>/dev/null || : > "$tmp/c3c.tmp"
cp "$tmp/c3c.tmp" "$DIR/runs/0008/meta.txt"
rm -f "$S8" "$S8R" || fixture_fail "rm sock"
: > "$tmp/shim-args"
"$HBIN" --config "$tmp/cfg.json" notify 33333333 'LIVE' < /dev/null \
    > "$tmp/c3c.out" 2> "$tmp/c3c.err"
rc3c=$?
_3c_ok=0; _3c_msg=""
if [ "$rc3c" -ne 0 ]; then
    _3c_msg="3c rc=$rc3c (want 0)"
elif [ -s "$tmp/shim-args" ]; then
    _3c_msg="3c the inject fired although the run's lead.sock is absent (it must not): $(tr '\n' ' ' < "$tmp/shim-args" 2>/dev/null | head_bytes 160)"
else
    _3c_ok=1
fi
# 3d: the scratch inbox kills it. Recreate the real socket; pass --inbox.
bind_sock "$S8R" "$S8" "0008 (3d)"
: > "$tmp/shim-args"
"$HBIN" --config "$tmp/cfg.json" notify --inbox "$tmp/scratch-inbox" 44444444 'LIVE' \
    < /dev/null > "$tmp/c3d.out" 2> "$tmp/c3d.err"
rc3d=$?
_3d_ok=0; _3d_msg=""
if [ "$rc3d" -ne 0 ]; then
    _3d_msg="3d rc=$rc3d (want 0)"
elif [ -s "$tmp/shim-args" ]; then
    _3d_msg="3d the inject fired although a scratch inbox is set (it must not): $(tr '\n' ' ' < "$tmp/shim-args" 2>/dev/null | head_bytes 160)"
else
    _3d_ok=1
fi
# 3e (M1152-i): the S_ISSOCK half. Cell 3c covers an ABSENT socket; this pins
# a PLAIN FILE at the newest run's lead.sock path (a regular file is NOT a
# socket -- the reference's -S test / the port's S_ISSOCK(st.st_mode) half,
# the run-0026 WEAK finding's "no perturbation"). The inject must NOT fire.
rm -f "$S8" "$S8R" || fixture_fail "rm sock (3e)"
printf 'x' > "$S8" || fixture_fail "plain file at 0008 sock (3e)"
: > "$tmp/shim-args"
"$HBIN" --config "$tmp/cfg.json" notify 45454545 'LIVE' < /dev/null \
    > "$tmp/c3e.out" 2> "$tmp/c3e.err"
rc3e=$?
_3e_ok=0; _3e_msg=""
if [ "$rc3e" -ne 0 ]; then
    _3e_msg="3e rc=$rc3e (want 0)"
elif [ -s "$tmp/shim-args" ]; then
    _3e_msg="3e the inject fired although the newest run's lead.sock is a PLAIN FILE, not a socket (it must not; the S_ISSOCK half): $(tr '\n' ' ' < "$tmp/shim-args" 2>/dev/null | head_bytes 160)"
else
    _3e_ok=1
fi
# 3f (M1152-i): the trailing-byte strip. The reference's o=$(... 2>&1)
# (gate-note.sh:26) strips the reply's TRAILING newline(s) BEFORE `tr '\n' ' '
# | cut -c1-160`, so a reply `{"ok":true}\n` carries as `{"ok":true}` -- NO
# trailing space in the watch line. The port's flatten carried one space PER
# newline with no strip, so the same reply landed as `{"ok":true} ` (a trailing
# space, the one-byte divergence). The shim answers the reply ending in a
# newline; the byte after the flattened reply must not be a space.
{
    printf '#!/bin/sh\n'
    printf 'printf '\''{"ok":true}\\n'\''\n'
    printf 'exit 0\n'
} > "$DIR/bin/jichi" || fixture_fail "trailing-newline shim"
chmod 755 "$DIR/bin/jichi"
rm -f "$S8" || fixture_fail "rm plain file (3f)"
bind_sock_soft "$S8R" "$S8"
: > "$tmp/shim-args"
"$HBIN" --config "$tmp/cfg.json" notify 46464646 'LIVE' < /dev/null \
    > "$tmp/c3f.out" 2> "$tmp/c3f.err"
rc3f=$?
_3f_ok=0; _3f_msg=""
if [ "$rc3f" -ne 0 ]; then
    _3f_msg="3f rc=$rc3f (want 0)"
elif ! grep -qE 'gate 46464646: note [0-9]+-[0-9]+-gate-46464646\.md; run 0008 inject rc=0 \{"ok":true\}([^ ].*)?$' "$tmp/c3f.out" 2>/dev/null; then
    _3f_msg="3f the stdout watch line did not carry the reply ending at {\"ok\":true} with NO trailing space (the reference's o=\$(... 2>&1) strips the reply's trailing newline before tr -- a trailing space is the one-byte divergence): $(tr '\n' '|' < "$tmp/c3f.out" 2>/dev/null | head_bytes 200)"
else
    _3f_ok=1
fi
# Restore the recording shim (cell 9 does not need the inject, but the fixture
# is left as the driver found it) and the live socket.
{
    printf '#!/bin/sh\n'
    printf 'for _a in "$@"; do printf '\''%%s\\n'\'' "$_a" >> %s/shim-args; done\n' "$tmp"
    printf 'echo "{\\"ok\\":true}"\n'
    printf 'exit 0\n'
} > "$DIR/bin/jichi" || fixture_fail "restore shim (3f)"
chmod 755 "$DIR/bin/jichi"
bind_sock_soft "$S8R" "$S8"

if [ "$_3a_ok" -eq 1 ] && [ "$_3b_ok" -eq 1 ] && [ "$_3c_ok" -eq 1 ] && [ "$_3d_ok" -eq 1 ] && [ "$_3e_ok" -eq 1 ] && [ "$_3f_ok" -eq 1 ]; then
    t_ok "3: the live-run detection, all three halves -- 3a fires (the pinned shim gets control <sock> inject <note body>) when the newest run is live with a real socket; 3b end= kills it; 3c an absent socket kills it; 3d the scratch inbox kills it; 3e a plain (non-socket) file at the lead.sock path kills it (the S_ISSOCK half); 3f a reply ending in a newline carries NO trailing space in the watch line (the reference's o=\$(... 2>&1) strip)"
else
    t_fail "3: the live-run detection misbehaved -- a:$([ -n "$_3a_msg" ] && printf '%s' "$_3a_msg") b:$([ -n "$_3b_msg" ] && printf '%s' "$_3b_msg") c:$([ -n "$_3c_msg" ] && printf '%s' "$_3c_msg") d:$([ -n "$_3d_msg" ] && printf '%s' "$_3d_msg") e:$([ -n "$_3e_msg" ] && printf '%s' "$_3e_msg") f:$([ -n "$_3f_msg" ] && printf '%s' "$_3f_msg")"
fi

# --- 4: the digest line ------------------------------------------------------
rm -f "$DIR/gate/digest.md" || fixture_fail "rm digest"
now=$(date '+%F %T %z')
"$HBIN" --config "$tmp/cfg.json" notify 55555555 'first' < /dev/null \
    > "$tmp/c4a.out" 2> "$tmp/c4a.err"
"$HBIN" --config "$tmp/cfg.json" notify 66666666 'second' < /dev/null \
    > "$tmp/c4b.out" 2> "$tmp/c4b.err"
rc4b=$?
if [ -f "$DIR/gate/digest.md" ]; then
    _dl=$(wc -l < "$DIR/gate/digest.md"); [ -n "$_dl" ] || _dl=0
else
    _dl=0
fi
_l55=$(grep -nF ' gate 55555555: first' "$DIR/gate/digest.md" 2>/dev/null | head -n 1 | cut -d: -f1)
_l66=$(grep -nF ' gate 66666666: second' "$DIR/gate/digest.md" 2>/dev/null | head -n 1 | cut -d: -f1)
if [ "$rc4b" -ne 0 ]; then
    t_fail "4: the digest notify exited $rc4b (want 0) -- on the red tree the \`notify\` verb does not exist: $(tr '\n' ' ' < "$tmp/c4b.err" | head_bytes 200)"
elif [ "$_dl" -ne 2 ]; then
    t_fail "4: digest.md has $_dl line(s) (want exactly 2) -- the digest line was not appended: $(tr '\n' '|' < "$DIR/gate/digest.md" 2>/dev/null | head_bytes 200)"
elif [ -z "$_l55" ] || [ -z "$_l66" ]; then
    t_fail "4: digest.md lacks the <now> gate <s>: <text> lines for 55555555/first or 66666666/second: $(tr '\n' '|' < "$DIR/gate/digest.md" 2>/dev/null | head_bytes 200)"
elif [ "$_l55" -gt "$_l66" ]; then
    t_fail "4: the digest lines are out of order (55555555 at line $_l55 is after 66666666 at line $_l66): $(tr '\n' '|' < "$DIR/gate/digest.md" 2>/dev/null | head_bytes 200)"
else
    t_ok "4: the digest line -- digest.md carries exactly two <now> gate <s>: <text> lines, 55555555: first before 66666666: second (the <now> minute is not asserted to the second)"
fi

# --- 5: the hook, CHANGED shape (the digest PATH, not the TEXT) --------------
rm -f "$tmp/hook-args" || fixture_fail "rm hook-args"
"$HBIN" --config "$tmp/cfg.json" notify 77777777 'hooked' < /dev/null \
    > "$tmp/c5a.out" 2> "$tmp/c5a.err"
rc5a=$?
_hook_ok=0; _hook_msg=""
if [ "$rc5a" -ne 0 ]; then
    _hook_msg="rc=$rc5a (want 0) -- on the red tree the notify verb does not exist: $(tr '\n' ' ' < "$tmp/c5a.err" 2>/dev/null | head_bytes 160)"
elif ! grep -qxF "$DIR/gate/digest.md" "$tmp/hook-args" 2>/dev/null; then
    _hook_msg="the hook did not receive the DIGEST PATH as an argument: $(tr '\n' ' ' < "$tmp/hook-args" 2>/dev/null | head_bytes 160)"
else
    _hook_ok=1
fi
# A failing hook (rc 3) must NOT change notify's rc.
printf '#!/bin/sh\nfor _a in "$@"; do printf '\''%%s\\n'\'' "$_a" >> %s/hook-args; done\nexit 3\n' \
    "$tmp" > "$tmp/hook.sh" || fixture_fail "failing hook stub"
chmod 755 "$tmp/hook.sh"
rm -f "$tmp/hook-args"
"$HBIN" --config "$tmp/cfg.json" notify 88888888 'hookfail' < /dev/null \
    > "$tmp/c5b.out" 2> "$tmp/c5b.err"
rc5b=$?
_hook_fail_ok=0; _hook_fail_msg=""
if [ "$rc5b" -ne 0 ]; then
    _hook_fail_msg="a failing hook (rc 3) changed notify's rc to $rc5b (want 0)"
elif [ ! -s "$tmp/hook-args" ]; then
    _hook_fail_msg="the failing hook never ran (its arg file is empty)"
else
    _hook_fail_ok=1
fi
# An ABSENT hook runs nothing and errors nothing.
printf '{"dir":"%s","modelKeyFile":"%s/kfile","gitTokenFile":"%s/tokfile","host":{"gateHome":"%s/gate"}}\n' \
    "$DIR" "$tmp" "$tmp" "$DIR" > "$tmp/cfg-nohook.json" || fixture_fail "nohook cfg"
rm -f "$tmp/hook-args"
"$HBIN" --config "$tmp/cfg-nohook.json" notify 99999999 'nohook' < /dev/null \
    > "$tmp/c5c.out" 2> "$tmp/c5c.err"
rc5c=$?
_hook_absent_ok=0; _hook_absent_msg=""
if [ "$rc5c" -ne 0 ]; then
    _hook_absent_msg="the absent-hook notify exited $rc5c (want 0): $(tr '\n' ' ' < "$tmp/c5c.err" 2>/dev/null | head_bytes 160)"
elif [ -e "$tmp/hook-args" ]; then
    _hook_absent_msg="an ABSENT hook ran something (its arg file exists)"
else
    _hook_absent_ok=1
fi
# Restore the clean hook stub (the original config was never modified).
printf '#!/bin/sh\nfor _a in "$@"; do printf '\''%%s\\n'\'' "$_a" >> %s/hook-args; done\nexit 0\n' \
    "$tmp" > "$tmp/hook.sh"
chmod 755 "$tmp/hook.sh"
: > "$tmp/hook-args"

if [ "$_hook_ok" -eq 1 ] && [ "$_hook_fail_ok" -eq 1 ] && [ "$_hook_absent_ok" -eq 1 ]; then
    t_ok "5: the hook (CHANGED shape) -- a config-named command runs with the DIGEST PATH (not the TEXT); a failing hook (rc 3) never changes notify's rc; an ABSENT hook runs nothing and errors nothing"
else
    t_fail "5: the hook misbehaved -- present:$([ -n "$_hook_msg" ] && printf '%s' "$_hook_msg") failing:$([ -n "$_hook_fail_msg" ] && printf '%s' "$_hook_fail_msg") absent:$([ -n "$_hook_absent_msg" ] && printf '%s' "$_hook_absent_msg")"
fi

# --- 6: ATTACH ----------------------------------------------------------------
printf 'a review\n' > "$tmp/attach.txt"
: > "$tmp/attach-empty"
now=$(date '+%F %T %z')
"$HBIN" --config "$tmp/cfg.json" notify aaaa0001 'with attach' "$tmp/attach.txt" \
    < /dev/null > "$tmp/c6a.out" 2> "$tmp/c6a.err"
rc6a=$?
_att_note=$(newest_gate_note "$DIR/inbox")
_att_ok=0; _att_msg=""
if [ "$rc6a" -ne 0 ]; then
    _att_msg="rc=$rc6a (want 0)"
elif [ -z "$_att_note" ] || ! { grep -qF '## Attached: attach.txt' "$_att_note" && grep -qF 'a review' "$_att_note"; } 2>/dev/null; then
    _att_msg="the note with a non-empty ATTACH lacks the ## Attached: attach.txt section and its contents: $(ls -t "$DIR"/inbox/*.md 2>/dev/null | head -n 3 | tr '\n' ' ')"
else
    _att_ok=1
fi
# EMPTY attach: no section.
"$HBIN" --config "$tmp/cfg.json" notify bbbb0002 'empty attach' "$tmp/attach-empty" \
    < /dev/null > "$tmp/c6b.out" 2> "$tmp/c6b.err"
rc6b=$?
_e_note=$(newest_gate_note "$DIR/inbox")
_e_ok=0; _e_msg=""
if [ "$rc6b" -ne 0 ]; then
    _e_msg="empty-attach rc=$rc6b (want 0)"
elif [ -n "$_e_note" ] && grep -qF '## Attached:' "$_e_note" 2>/dev/null; then
    _e_msg="an EMPTY attach file added a ## Attached: section (the reference's [ -s ] test must skip it): $(grep -F '## Attached:' "$_e_note" 2>/dev/null | head_bytes 120)"
else
    _e_ok=1
fi
# MISSING attach: no section.
"$HBIN" --config "$tmp/cfg.json" notify cccc0003 'missing attach' "$tmp/attach-missing" \
    < /dev/null > "$tmp/c6c.out" 2> "$tmp/c6c.err"
rc6c=$?
_m_note=$(newest_gate_note "$DIR/inbox")
_m_ok=0; _m_msg=""
if [ "$rc6c" -ne 0 ]; then
    _m_msg="missing-attach rc=$rc6c (want 0)"
elif [ -n "$_m_note" ] && grep -qF '## Attached:' "$_m_note" 2>/dev/null; then
    _m_msg="a MISSING attach path added a ## Attached: section (the [ -s ] test must skip it): $(grep -F '## Attached:' "$_m_note" 2>/dev/null | head_bytes 120)"
else
    _m_ok=1
fi
if [ "$_att_ok" -eq 1 ] && [ "$_e_ok" -eq 1 ] && [ "$_m_ok" -eq 1 ]; then
    t_ok "6: ATTACH -- a non-empty attach file adds a ## Attached: attach.txt section with its contents (a review); an EMPTY and a MISSING path add no section (the reference's [ -s ] test)"
else
    t_fail "6: ATTACH misbehaved -- present:$([ -n "$_att_msg" ] && printf '%s' "$_att_msg") empty:$([ -n "$_e_msg" ] && printf '%s' "$_e_msg") missing:$([ -n "$_m_msg" ] && printf '%s' "$_m_msg")"
fi

# --- 7: the usage refusal -----------------------------------------------------
_before=$(ls "$DIR/inbox" 2>/dev/null | grep -c '\-gate-')
[ -n "$_before" ] || _before=0
"$HBIN" --config "$tmp/cfg.json" notify < /dev/null > "$tmp/c7a.out" 2> "$tmp/c7a.err"
rc7a=$?
"$HBIN" --config "$tmp/cfg.json" notify dddd0004 < /dev/null > "$tmp/c7b.out" 2> "$tmp/c7b.err"
rc7b=$?
_after=$(ls "$DIR/inbox" 2>/dev/null | grep -c '\-gate-')
[ -n "$_after" ] || _after=0
_usage_ok=0; _usage_msg=""
if [ "$rc7a" -ne 2 ]; then
    _usage_msg="no-arg notify exited $rc7a (want 2): $(tr '\n' ' ' < "$tmp/c7a.err" 2>/dev/null | head_bytes 160)"
elif ! grep -qiE 'unknown|notify|usage|SHA8' "$tmp/c7a.err" 2>/dev/null; then
    _usage_msg="no-arg notify's stderr names no usage/notify: $(tr '\n' ' ' < "$tmp/c7a.err" 2>/dev/null | head_bytes 160)"
elif [ "$rc7b" -ne 2 ]; then
    _usage_msg="SHA8-only notify exited $rc7b (want 2): $(tr '\n' ' ' < "$tmp/c7b.err" 2>/dev/null | head_bytes 160)"
elif [ "$_after" -ne "$_before" ]; then
    _usage_msg="the usage refusal wrote a note (inbox grew $_before -> $_after) -- it must refuse BEFORE any write"
else
    _usage_ok=1
fi
if [ "$_usage_ok" -eq 1 ]; then
    t_ok "7: the usage refusal -- notify with no SHA8/TEXT (and SHA8-only) exits 2 with a usage refusal on stderr, and writes NO note (the reference's \${1:?}/\${2:?} class)"
else
    t_fail "7: the usage refusal misbehaved -- $_usage_msg"
fi

# --- 8: the flatten carries the bytes (one space per newline) ---------------
# A shim whose inject reply has a BLANK line: the reference flattens the reply
# with `tr '\n' ' ' | cut -c1-160` -- ONE SPACE PER NEWLINE, no coalescing.
# "line1\n\nline3" therefore carries to the stdout watch line as
# "line1  line3" (TWO spaces). The review found the port's pending_space logic
# collapsed the run of newlines to a single space ("line1 line3") -- the inj
# string's BYTES are carried, and coalescing broke them.
{
    printf '#!/bin/sh\n'
    printf 'printf '\''line1\\n\\nline3\\n'\''\n'
    printf 'exit 0\n'
} > "$DIR/bin/jichi" || fixture_fail "blank-line shim"
chmod 755 "$DIR/bin/jichi"
"$HBIN" --config "$tmp/cfg.json" notify 8888aabb 'flatten' < /dev/null \
    > "$tmp/c8.out" 2> "$tmp/c8.err"
rc8=$?
_flat_ok=0; _flat_msg=""
if [ "$rc8" -ne 0 ]; then
    _flat_msg="rc=$rc8 (want 0): $(tr '\n' ' ' < "$tmp/c8.err" 2>/dev/null | head_bytes 160)"
elif ! grep -q 'run 0008 inject rc=0 line1  line3' "$tmp/c8.out" 2>/dev/null; then
    _flat_msg="the stdout watch line did not carry one space per newline (line1\\n\\nline3 must flatten to 'line1  line3', TWO spaces): $(tr '\n' '|' < "$tmp/c8.out" 2>/dev/null | head_bytes 200)"
else
    _flat_ok=1
fi
if [ "$_flat_ok" -eq 1 ]; then
    t_ok "8: the flatten carries the bytes -- a blank line in the inject reply (line1\\n\\nline3) flattens to line1  line3 (ONE SPACE PER NEWLINE, the reference's tr \\n ' ' | cut -c1-160, no coalescing) on the stdout watch line"
else
    t_fail "8: the flatten misbehaved -- $_flat_msg"
fi
# Restore the recording shim (cell 9 does not need the inject, but the fixture
# is left as the driver found it).
{
    printf '#!/bin/sh\n'
    printf 'for _a in "$@"; do printf '\''%%s\\n'\'' "$_a" >> %s/shim-args; done\n' "$tmp"
    printf 'echo "{\\"ok\\":true}"\n'
    printf 'exit 0\n'
} > "$DIR/bin/jichi" || fixture_fail "restore shim"
chmod 755 "$DIR/bin/jichi"

# --- 9: the over-cap refusal (CHANGED: a named exit-1, never a silent
# truncation) ----------------------------------------------------------------
# The reference cats the ATTACH and prints the TEXT with NO bound. The port
# BOUNDS the TEXT (NOTIFY_TEXT_CAP, 128 KB) and the ATTACH (NOTIFY_ATTACH_CAP,
# 1 MB) as a memory bound and REFUSES over-cap input with a named exit-1
# before any write (no note, no digest line, no hook) -- a silently truncated
# verdict is a wrong verdict (the same class as the four false verdicts this
# port exists to prevent).
#
# THE TEXT FIXTURE IS 131071 BYTES (the kernel's per-argv limit), NOT the task
# brief's 200000. On Linux a SINGLE argv element is capped at MAX_ARG_STRLEN =
# 131072 = NOTIFY_TEXT_CAP (the kernel's exec refuses longer args with E2BIG,
# exit 126), so a 200000-byte TEXT dies in the exec before notify ever runs --
# before AND after the fix, indistinguishable, so it cannot prove the port's
# named refusal. 131071 is the LARGEST argv-deliverable TEXT: it is over the
# port's refusal threshold (NOTIFY_TEXT_CAP minus the 256-byte prefix headroom
# = 130816), so the RED (the old code silently truncated it into the 128 KB
# note buffer and exited 0, writing a wrong note) is real, and the GREEN (a
# named exit-1, no write) is the fix. The ATTACH is file-based, so it has no
# argv limit and is the full 2000000 (2 MB). Both fixtures are driver-built
# (the cell-14 BIG idiom: head_bytes N < /dev/zero | tr), not a cap on the run.
head_bytes 131071 < /dev/zero | tr '\0' 'x' > "$tmp/bigtext" || fixture_fail "big text"
_bigtext=$(cat "$tmp/bigtext"); [ ${#_bigtext} -eq 131071 ] || fixture_fail "big text size ${#_bigtext}"
head_bytes 2000000 < /dev/zero | tr '\0' 'y' > "$tmp/bigattach" || fixture_fail "big attach"
_dig_before=$(wc -l < "$DIR/gate/digest.md" 2>/dev/null); [ -n "$_dig_before" ] || _dig_before=0
"$HBIN" --config "$tmp/cfg.json" notify eeee0009 "$_bigtext" < /dev/null \
    > "$tmp/c9a.out" 2> "$tmp/c9a.err"
rc9a=$?
"$HBIN" --config "$tmp/cfg.json" notify ffff0009 'small text' "$tmp/bigattach" < /dev/null \
    > "$tmp/c9b.out" 2> "$tmp/c9b.err"
rc9b=$?
_dig_after=$(wc -l < "$DIR/gate/digest.md" 2>/dev/null); [ -n "$_dig_after" ] || _dig_after=0
_t_notes=$(ls "$DIR/inbox" 2>/dev/null | grep -c '\-gate-eeee0009\.md$'); [ -n "$_t_notes" ] || _t_notes=0
_a_notes=$(ls "$DIR/inbox" 2>/dev/null | grep -c '\-gate-ffff0009\.md$'); [ -n "$_a_notes" ] || _a_notes=0
_cap_ok=0; _cap_msg=""
if [ "$rc9a" -ne 1 ]; then
    _cap_msg="over-cap TEXT (131071 bytes, the argv limit) exited $rc9a (want 1, the named refusal -- before the fix the port truncated it into the 128 KB note buffer and exited 0)"
elif ! { grep -qF '131071' "$tmp/c9a.err" 2>/dev/null && grep -qF '131072' "$tmp/c9a.err" 2>/dev/null && grep -qF 'NOTIFY_TEXT_CAP' "$tmp/c9a.err" 2>/dev/null; }; then
    _cap_msg="the over-cap TEXT refusal does not name the cap and the actual size on stderr: $(tr '\n' ' ' < "$tmp/c9a.err" 2>/dev/null | head_bytes 200)"
elif [ "$_t_notes" -ne 0 ]; then
    _cap_msg="the over-cap TEXT refusal wrote a note ($_t_notes matching *-gate-eeee0009.md) -- it must refuse BEFORE any write"
elif [ "$rc9b" -ne 1 ]; then
    _cap_msg="over-cap ATTACH (2000000 bytes) exited $rc9b (want 1, the named refusal -- before the fix the port truncated it to 1 MB and exited 0)"
elif ! { grep -qF '2000000' "$tmp/c9b.err" 2>/dev/null && grep -qF '1048576' "$tmp/c9b.err" 2>/dev/null && grep -qF 'NOTIFY_ATTACH_CAP' "$tmp/c9b.err" 2>/dev/null; }; then
    _cap_msg="the over-cap ATTACH refusal does not name the cap and the actual size on stderr: $(tr '\n' ' ' < "$tmp/c9b.err" 2>/dev/null | head_bytes 200)"
elif [ "$_a_notes" -ne 0 ]; then
    _cap_msg="the over-cap ATTACH refusal wrote a note ($_a_notes matching *-gate-ffff0009.md) -- it must refuse BEFORE any write"
elif [ "$_dig_after" -ne "$_dig_before" ]; then
    _cap_msg="the over-cap refusal appended a digest line ($_dig_before -> $_dig_after) -- it must refuse BEFORE any write"
else
    _cap_ok=1
fi
if [ "$_cap_ok" -eq 1 ]; then
    t_ok "9: the over-cap refusal (CHANGED) -- a 131071-byte TEXT (the argv limit, over the NOTIFY_TEXT_CAP, 128 KB, note bound) and a 2 MB ATTACH (over NOTIFY_ATTACH_CAP, 1 MB) each exit 1 with the cap and the actual size named on stderr, and write NO note / NO digest line (a silently truncated verdict is a wrong verdict)"
else
    t_fail "9: the over-cap refusal misbehaved -- $_cap_msg"
fi

t_done
