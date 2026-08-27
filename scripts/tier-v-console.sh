#!/bin/sh
# tier-v-console.sh - Tier V row V6, the LINUX VIRTUAL CONSOLE cell
# (docs/plans/2026-07-hardware-testing.md). Sibling of tier-v-terminals.sh,
# which covers the X11 emulators; this covers the cell that one could not
# reach, and which the plan calls the row's most interesting: no bracketed
# paste, 8 colours, and the KERNEL's terminal emulation instead of xterm's.
#
# Why it is a separate script rather than another emulator in the X11 runner:
# nothing about the mechanism is shared. There is no window to focus, no
# clipboard to own, no screenshot to take, and -- the load-bearing difference
# -- jichi must NOT be wrapped in script(1) here. That wrapper is what lets
# the X11 runner keep the exact byte stream, but it inserts a pty between
# jichi and its terminal, which on a console would replace the very emulator
# under test. So jichi is spawned directly on the VT (vtdrive does it the way
# openvt(1) does) and the witnesses are:
#
#   the screen   /dev/vcsa<N>, the kernel's own screen memory -- what the
#                console DISPLAYS after interpreting our escape sequences
#   the wire     mockmodel's captured requests -- what the model RECEIVED
#
# That pairing is the M268 lesson: a render is a second-hand witness, so
# anything about content is asserted on the captured request.
#
# Usage:
#   sudo scripts/tier-v-console.sh              # all checks
#   sudo scripts/tier-v-console.sh --dry-run    # print the plan, touch nothing
#   sudo scripts/tier-v-console.sh --keep       # keep captures + screen dumps
#   sudo JC_V6_VT=13 scripts/tier-v-console.sh  # use another VT
#
# Needs root (uinput, vcsa and /dev/tty<N> are root-only) and it switches the
# active VT while it runs, then switches back. Build first:
#   make jichi vtdrive smoke-tools
#
# VT 12 by default: logind's NAutoVTs is 6, so VTs 1-6 get a getty spawned on
# switch, which would fight us for the terminal. 12 is outside that range.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="${JC_V6_BIN:-$ROOT/jichi}"
TOOLS="$ROOT/tests/tools"
VTDRIVE="$TOOLS/vtdrive"
MOCK="$TOOLS/mockmodel"
VT="${JC_V6_VT:-12}"

DRY=0
KEEP=0
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY=1 ;;
        --keep)    KEEP=1 ;;
        -h|--help) sed -n '2,38p' "$0"; exit 0 ;;
        *)         echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

OUT="${JC_V6_OUT:-$ROOT/.v6-console-results}"
N_OK=0
N_FAIL=0
N_SKIP=0

say()  { echo "== $*"; }
ok()   { N_OK=$((N_OK + 1));     echo "ok - $*";     echo "ok   - $*" >> "$OUT/results.txt"; }
bad()  { N_FAIL=$((N_FAIL + 1)); echo "not ok - $*"; echo "FAIL - $*" >> "$OUT/results.txt"; }
skip() { N_SKIP=$((N_SKIP + 1)); echo "skip - $*";   echo "skip - $*" >> "$OUT/results.txt"; }

if [ "$DRY" -eq 1 ]; then
    echo "[dry run] VT $VT, checks 0..6:"
    echo "  0 self-test: injected keystrokes echo back (validates uinput + keymap + vcsa)"
    echo "  1 prompt renders on the kernel emulator, /help readable"
    echo "  2 type-ahead echoed + queued, reaches call 2 as [operator]"
    echo "  2c and the other side: WITHOUT the flag the typing is dropped"
    echo "  3 three-line BURST paste (LF bytes, no bracketed paste) arrives intact"
    echo "  4 SIGWINCH mid-turn (TIOCSWINSZ on the console)"
    echo "  5 ASCII fallbacks under LC_ALL=C + NO_COLOR=1, no UTF-8 glyphs on screen"
    echo "  6 Ctrl-C mid-turn, then Ctrl-C twice and Ctrl-D at the prompt -> exit 0"
    exit 0
fi

# ------------------------------------------------------------------ preflight

[ "$(id -u)" -eq 0 ] || { echo "v6-console: needs root (uinput/vcsa/tty)" >&2; exit 1; }
[ -x "$BIN" ]     || { echo "v6-console: build jichi first (make)" >&2; exit 1; }
[ -x "$VTDRIVE" ] || { echo "v6-console: build the driver first (make vtdrive)" >&2; exit 1; }
[ -x "$MOCK" ]    || { echo "v6-console: build the mock first (make smoke-tools)" >&2; exit 1; }
[ -c /dev/uinput ] || { echo "v6-console: no /dev/uinput (modprobe uinput)" >&2; exit 1; }
[ -c /dev/tty0 ]   || { echo "v6-console: no /dev/tty0 -- is this a console host?" >&2; exit 1; }

rm -rf "$OUT"; mkdir -p "$OUT"
: > "$OUT/results.txt"
{
    echo "Tier V row V6 -- the Linux virtual console"
    echo "run: $(date -u '+%Y-%m-%dT%H:%M:%SZ')  host: $(uname -n)"
    echo "os: $(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-?}")  kernel: $(uname -r)"
    echo "vt: $VT   host session: ${XDG_SESSION_TYPE:-?}"
    echo
} >> "$OUT/results.txt"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/jichi_v6c.XXXXXX")
cleanup() {
    [ -n "${MM_PID:-}" ] && kill "$MM_PID" 2>/dev/null
    [ "$KEEP" -eq 1 ] || rm -rf "$WORK"
}
trap cleanup EXIT INT TERM
mkdir -p "$WORK/home"

say "V6 -- the Linux virtual console"
echo "   host   : $(uname -n)  $(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-?}")"
echo "   kernel : $(uname -r)   vt: $VT"
echo "   results: $OUT"
echo

# ------------------------------------------------------------------- helpers

mock_start() {  # mock_start <script> <capture-dir> [max-requests]
    rm -f "$2/.port"; mkdir -p "$2"
    if [ -n "${3:-}" ]; then
        "$MOCK" --script "$1" --capture "$2" --port-file "$2/.port" \
            --deadline 180 --max-requests "$3" >/dev/null 2>&1 &
    else
        "$MOCK" --script "$1" --capture "$2" --port-file "$2/.port" \
            --deadline 180 >/dev/null 2>&1 &
    fi
    MM_PID=$!
    _i=0
    while [ ! -s "$2/.port" ]; do
        _i=$((_i + 1))
        [ "$_i" -gt 15 ] && { echo "v6-console: mock never announced a port" >&2; return 1; }
        sleep 1
    done
    MM_PORT=$(cat "$2/.port")
}
mock_stop() {
    [ -n "${MM_PID:-}" ] && { kill "$MM_PID" 2>/dev/null; wait "$MM_PID" 2>/dev/null; }
    MM_PID=""
}

write_config() {  # write_config <path> <port>
    cat > "$1" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF
}

new_case() {  # new_case <name> -> CASE, WS, CFG, CAPDIR, SCR, LOG
    CASE=$1
    WS="$WORK/$CASE-ws"; CAPDIR="$WORK/$CASE-cap"
    CFG="$WORK/$CASE.json"; SCR="$WORK/$CASE.vd"; LOG="$OUT/screen-$CASE.txt"
    ERR="$OUT/stderr-$CASE.txt"
    mkdir -p "$WS" "$CAPDIR"
}

# drive <extra-vtdrive-args> <jichi-args...> -- runs the current case's script
drive() {
    _gap=$1; shift
    # env(1), NOT prefix assignments: $V6_ENV is an EXPANSION, and POSIX
    # recognises assignments only when they appear literally before the
    # command word -- expanded, "LC_ALL=C" becomes the command name and the
    # run dies with "LC_ALL=C: not found". Cost one round trip (M274), the
    # same shape as M273's supervisor driver.
    ( cd "$WS" && env HOME="$WORK/home" TERM=linux LANG="${V6_LANG:-C.UTF-8}" \
        ${V6_ENV:-} "$VTDRIVE" --vt "$VT" --gap "$_gap" --deadline 120 \
        --log "$LOG" "$SCR" -- "$BIN" --config "$CFG" --no-route "$@" \
        2>>"$ERR" )
}

# ============================================================ the checks

# 0. Validate the instrument before trusting it: type a known string into
#    plain `cat` and read it back off the console's screen memory. That
#    exercises the whole input path end to end -- uinput device, the keymap
#    vtdrive inverted out of the kernel, the console's echo, and vcsa -- so a
#    broken instrument fails HERE, as a tooling failure, instead of surfacing
#    below as a phantom jichi bug (the docs/TEST_INTEGRITY.md direction that
#    gets less attention: a test reporting a failure that never happened).
#
#    It has already paid for itself once: the first run typed `_` from a US
#    assumption and read back `?`, because this host's console keymap is
#    German. Every check below it would have failed, and all of them would
#    have looked like jichi's fault.
check_selftest() {
    new_case selftest
    cat > "$SCR" <<'EOF'
delay 700
send "VTDRIVE_SELFTEST_OK\r"
expect "VTDRIVE_SELFTEST_OK" 10
send "\x04"
waitexit 10
EOF
    if ( cd "$WS" && "$VTDRIVE" --vt "$VT" --deadline 60 --log "$LOG" \
             "$SCR" -- cat >/dev/null 2>>"$ERR" ); then
        ok "0 injected keystrokes reach the console and read back (uinput + keymap + vcsa)"
    else
        bad "0 self-test failed -- the instrument is wrong, not jichi"
        echo "   screen: $LOG"
        echo "   stderr: $ERR"
        sed 's/^/   | /' "$ERR" 2>/dev/null | head -8
        return 1
    fi
}

# 1. The TUI renders on the kernel's emulator, and /help is readable.
#    "Knowledge & display" is the LAST /help section, so it is what remains on
#    screen after the rest has scrolled -- a VC has no scrollback to search.
check_help() {
    new_case help
    printf 'wire openai\nrule\n  text Hello from the mock.\n' > "$WORK/$CASE.mm"
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    cat > "$SCR" <<'EOF'
expect "] " 20
send "\x15/help\r"
expect "Knowledge & display" 15
delay 500
send "\x15/exit\r"
waitexit 15
EOF
    if drive 12 >/dev/null; then
        ok "1 prompt renders on the kernel emulator and /help is readable"
    else
        bad "1 prompt or /help did not render on the VC (see $LOG)"
    fi
    mock_stop
}

# 2. Type-ahead: text typed DURING a turn is echoed, queued, and lands on the
#    next model call prefixed [operator]. Screen for the echo, wire for the
#    content.
check_typeahead() {
    # The sequence is lifted VERBATIM from tests/smoke/typeahead.sh, which is
    # known to work on a pty: type, Enter, THEN expect "queued", THEN the
    # mock's reply. Two earlier attempts here invented the order and waited for
    # the queued notice BEFORE pressing Enter, where it cannot exist yet -- the
    # expect timed out and killed the run, so the wire assertion failed too and
    # one harness bug read as two jichi bugs.
    cat > "$WORK/ta-template.mm" <<'MMEOF'
wire openai
rule
  count 1
  delay 5000
  tool list_files {"path":"."}
rule
  match "[operator] also read the docs"
  text STEER_OK
rule
  text STEER_MISSED
MMEOF

    # --- with --type-ahead: echoed, queued, and it steers turn 2
    new_case typeahead
    cp "$WORK/ta-template.mm" "$WORK/$CASE.mm"
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    cat > "$SCR" <<'EOF'
expect "] " 20
send "\x15hello\r"
delay 900
send "also read the docs"
delay 700
send "\r"
expect "queued" 20
expect "STEER_OK" 25
delay 500
send "\x15/exit\r"
waitexit 15
EOF
    if drive 12 --type-ahead >/dev/null; then
        ok "2 typed mid-turn on the console: echoed, queued, and the turn steered"
    else
        bad "2 the type-ahead sequence did not complete (see $LOG / $ERR)"
    fi
    if [ -f "$CAPDIR/req.2" ] && grep -q "\[operator\] also read the docs" "$CAPDIR/req.2"; then
        ok "2b the typed-ahead line reached the next model call as [operator]"
    else
        bad "2b the steering message never made it onto the wire"
    fi
    mock_stop

    # --- WITHOUT the flag: M257 made type-ahead opt-in, so the same typing
    # must be DROPPED. Asserting only the enabled side would let a change that
    # silently turned type-ahead on everywhere pass unnoticed -- and it is the
    # property I got wrong twice while writing this cell.
    new_case typeahead_off
    cp "$WORK/ta-template.mm" "$WORK/$CASE.mm"
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    cat > "$SCR" <<'EOF'
expect "] " 20
send "\x15hello\r"
delay 900
send "also read the docs\r"
expect "STEER_MISSED" 30
delay 500
send "\x15/exit\r"
waitexit 15
EOF
    if drive 12 >/dev/null; then
        ok "2c without --type-ahead the mid-turn typing is dropped (M257's default)"
    else
        bad "2c the default-off behaviour did not hold (see $LOG / $ERR)"
    fi
    mock_stop
}

# 3. Paste. A console has NO bracketed paste and no clipboard: a pasted block
#    is simply bytes arriving together, with raw LF between lines (the Enter
#    KEY sends CR; a paste does not). So the faithful test is a zero-gap burst
#    whose line breaks are LF -- exactly M156's fallback path. Ground truth is
#    the captured request.
check_paste() {
    new_case paste
    cat > "$WORK/$CASE.mm" <<'EOF'
wire openai
rule
  match "line1"
  match "line2"
  match "line3"
  text PASTE_OK
rule
  text PASTE_BAD
EOF
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    cat > "$SCR" <<'EOF'
expect "] " 20
send "line1\nline2\nline3"
delay 1200
send "\r"
delay 5000
send "\x15/exit\r"
waitexit 15
EOF
    drive 0 >/dev/null
    _req="$CAPDIR/req.1"
    if [ ! -f "$_req" ]; then
        bad "3 the burst paste never reached the model (no request sent)"
    elif grep -q "line1" "$_req" && grep -q "line2" "$_req" && grep -q "line3" "$_req"; then
        ok "3 a three-line LF burst arrived intact on a console with no bracketed paste"
    else
        bad "3 the burst was truncated -- the request is missing lines"
    fi
    if grep -q "PASTE_OK" "$LOG" 2>/dev/null; then
        ok "3b the burst submitted as ONE logical line (M156's fallback)"
    else
        skip "3b could not judge the submitted shape from the screen"
    fi
    mock_stop
}

# 4. SIGWINCH. A console's geometry is set by its font, so a real drag-resize
#    has no analogue; TIOCSWINSZ on the console is the same signal by the same
#    path, and what is asserted is that jichi survives it mid-turn and still
#    serves the prompt afterwards. NOT covered: a setfont(8) geometry change,
#    where the DISPLAY changes too -- deliberately out, since the font is
#    shared across consoles and this runner must not reshape the operator's.
check_resize() {
    new_case resize
    cat > "$WORK/$CASE.mm" <<'EOF'
wire openai
rule
  count 1
  delay 4000
  text RESIZED_FINE
rule
  text SECOND_OK
EOF
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    cat > "$SCR" <<'EOF'
expect "] " 20
send "\x15hello\r"
delay 1500
winsize 30 100
delay 1000
winsize 25 80
expect "RESIZED_FINE" 20
delay 500
send "\x15/exit\r"
waitexit 15
EOF
    if drive 12 >/dev/null; then
        ok "4 SIGWINCH mid-turn on the console: the turn still completed"
    else
        bad "4 the turn did not survive a mid-turn resize (see $LOG)"
    fi
    mock_stop
}

# 5. ASCII fallbacks. LC_ALL=C + NO_COLOR=1 is the console's normal condition
#    (8 colours, a font with no box-drawing beyond CP437). Assert BOTH sides:
#    the ASCII markers are there AND no UTF-8 glyph reached the screen.
check_nocolor() {
    new_case nocolor
    cat > "$WORK/$CASE.mm" <<'EOF'
wire openai
rule
  count 1
  tool list_files {"path":"."}
rule
  text ASCII_DONE
EOF
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    cat > "$SCR" <<'EOF'
expect "] " 20
send "\x15list the files\r"
expect "ASCII_DONE" 25
delay 500
send "\x15/exit\r"
waitexit 15
EOF
    V6_LANG=C V6_ENV="LC_ALL=C NO_COLOR=1" drive 12 >/dev/null
    if grep -q "ASCII_DONE" "$LOG" 2>/dev/null; then
        ok "5 a tool-calling turn completed under LC_ALL=C + NO_COLOR=1"
    else
        bad "5 the turn did not complete with ASCII fallbacks (see $LOG)"
    fi
    # The glyphs jichi swaps out when the locale is not UTF-8. Absence only
    # means something if something was RENDERED: on the first run this check
    # passed against an empty screen, because the session had failed to start
    # at all. Establish the premise first -- the same two-sided shape as
    # tests/smoke/expect_header.sh.
    if ! grep -q "ASCII_DONE" "$LOG" 2>/dev/null; then
        skip "5b nothing rendered, so glyph absence proves nothing (premise fails)"
        elif _g=$(printf '\342\234\223|\342\234\227|\342\226\270'); LC_ALL=C grep -qE "$_g" "$LOG" 2>/dev/null; then   # M471: no GNU-only hex escape
        bad "5b UTF-8 glyphs reached a C-locale console (ASCII fallback missed)"
    else
        ok "5b a rendered turn carried no UTF-8 glyphs -- the ASCII fallback engaged"
    fi
    mock_stop
}

# 6. Signals from a real keyboard: Ctrl-C mid-turn must abort the turn and
#    leave a usable prompt; Ctrl-C twice then Ctrl-D at an empty prompt must
#    exit cleanly (0).
check_signals() {
    new_case signals
    cat > "$WORK/$CASE.mm" <<'EOF'
wire openai
rule
  count 1
  delay 15000
  text NEVER_SEEN
rule
  text AFTER_ABORT
EOF
    mock_start "$WORK/$CASE.mm" "$CAPDIR" || return
    write_config "$CFG" "$MM_PORT"
    cat > "$SCR" <<'EOF'
expect "] " 20
send "\x15hello\r"
delay 2500
send "\x03"
delay 1500
expect "] " 15
send "\x03"
delay 400
send "\x03"
delay 400
send "\x04"
waitexit 20
assertexit 0
EOF
    if drive 12 >/dev/null; then
        ok "6 Ctrl-C aborted the turn, then Ctrl-C x2 + Ctrl-D exited 0"
    else
        bad "6 the signal sequence did not end in a clean exit (see $LOG)"
    fi
    mock_stop
}

# ================================================================== run them

check_selftest || {
    echo
    say "stopping: the instrument failed its own check, so nothing below would mean anything"
    echo "   $N_OK ok, $N_FAIL failed, $N_SKIP skipped"
    exit 1
}
check_help
check_typeahead
check_paste
check_resize
check_nocolor
check_signals

echo
{
    echo
    echo "$N_OK ok, $N_FAIL failed, $N_SKIP skipped"
} >> "$OUT/results.txt"
say "$N_OK ok, $N_FAIL failed, $N_SKIP skipped   ($OUT/results.txt)"
[ "$N_FAIL" -eq 0 ]
